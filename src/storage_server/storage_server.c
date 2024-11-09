// storage_server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "../common/protocol.h"

#define NM_PORT 9000

char base_dir[MAX_PATH_LENGTH];

void send_file_list(const char *nm_ip, int nm_port, SSRegisterInfo ss_info) {
    // Collect file list
    DIR *dir = opendir(base_dir);
    if (dir == NULL) {
        perror("Failed to open base directory");
        return;
    }
    struct dirent *dp;
    char file_paths[MAX_DATA_SIZE] = {0};
    char filepath[MAX_PATH_LENGTH * 2];
    struct stat statbuf;
    while ((dp = readdir(dir)) != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/%s", base_dir, dp->d_name);
        if (stat(filepath, &statbuf) == 0) {
            if (S_ISREG(statbuf.st_mode)) { // Check if it's a regular file
                strcat(file_paths, "/");
                strcat(file_paths, dp->d_name);
                strcat(file_paths, "\n");
            }
        }
    }
    closedir(dir);

    // Send file list to naming server
    int nm_sock;
    struct sockaddr_in nm_addr;
    if ((nm_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return;
    }
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("Invalid Naming Server IP");
        return;
    }
    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to Naming Server failed");
        return;
    }
    // Prepare file list update message
    Message msg;
    msg.type = MSG_FILE_LIST_UPDATE;
    SSFileListUpdate file_update;
    file_update.file_count = 0; // Not used in this example
    strcpy(file_update.file_paths, file_paths);
    memcpy(msg.payload, &file_update, sizeof(SSFileListUpdate));
    send(nm_sock, &msg, sizeof(msg), 0);
    close(nm_sock);
}

void *handle_client(void *arg) {
	int client_sock = *(int *)arg;
	free(arg);

	Message msg;
	int bytes_read = recv(client_sock, &msg, sizeof(msg), 0);
	if (bytes_read <= 0) {
		close(client_sock);
		pthread_exit(NULL);
	}

	if (msg.type == MSG_SS_REQUEST) {
		SSRequest ss_req;
		memcpy(&ss_req, msg.payload, sizeof(SSRequest));

		printf("Received request: %s %s\n",
			   ss_req.command, ss_req.path);

		Message ss_response;
		ss_response.type = MSG_SS_RESPONSE;

		// Prepend base directory to path
		char full_path[MAX_PATH_LENGTH * 2];
		snprintf(full_path, sizeof(full_path), "%s%s", base_dir, ss_req.path);

		if (strcmp(ss_req.command, "READ") == 0) {
			// Read file content
			FILE *file = fopen(full_path, "r");
			if (file == NULL) {
				ss_response.type = MSG_ERROR;
				strcpy(ss_response.payload, "File not found\n");
			} else {
				memset(ss_response.payload, 0, MAX_DATA_SIZE);
				fread(ss_response.payload, 1, MAX_DATA_SIZE, file);
				fclose(file);
			}
		} else if (strcmp(ss_req.command, "WRITE") == 0) {
			// Write data to file
			FILE *file = fopen(full_path, "w");
			if (file == NULL) {
				ss_response.type = MSG_ERROR;
				strcpy(ss_response.payload, "Failed to open file for writing\n");
			} else {
				ss_req.data[strlen(ss_req.data)] = '\n';
				ss_req.data[MAX_DATA_SIZE - 1] = '\0';
				fwrite(ss_req.data, 1, strlen(ss_req.data), file);
				fclose(file);
				strcpy(ss_response.payload, "Write successful\n");
			}
		} else if (strcmp(ss_req.command, "LIST") == 0) {
			// List directory contents
			DIR *dir = opendir(full_path);
			if (dir == NULL) {
				ss_response.type = MSG_ERROR;
				strcpy(ss_response.payload, "Directory not found\n");
			} else {
				struct dirent *dp;
				char buffer[MAX_DATA_SIZE] = {0};
				while ((dp = readdir(dir)) != NULL) {
					strcat(buffer, dp->d_name);
					strcat(buffer, "\n");
				}
				closedir(dir);
				strcpy(ss_response.payload, buffer);
			}
		} else {
			ss_response.type = MSG_ERROR;
			strcpy(ss_response.payload, "Unknown command\n");
		}

		send(client_sock, &ss_response, sizeof(ss_response), 0);
	}

	close(client_sock);
	pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
	if (argc < 4) {
		printf("Usage: %s <NM_IP> <SS_Port> <Base_Directory>\n", argv[0]);
		return -1;
	}

	char *nm_ip = argv[1];
	int ss_port = atoi(argv[2]);
	strcpy(base_dir, argv[3]);

	// Register with Naming Server
	int nm_sock;
	struct sockaddr_in nm_addr;

	if ((nm_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Socket creation error");
		return -1;
	}

	nm_addr.sin_family = AF_INET;
	nm_addr.sin_port = htons(NM_PORT);

	if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
		perror("Invalid Naming Server IP");
		return -1;
	}

	if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
		perror("Connection to Naming Server failed");
		return -1;
	}

	// Prepare registration message
	Message reg_msg;
	reg_msg.type = MSG_REGISTER_SS;

	SSRegisterInfo ss_info;
	// Get local IP address
	char ss_ip[16];
	// Assuming localhost for example
	strcpy(ss_ip, "127.0.0.1");
	strcpy(ss_info.ip_address, ss_ip);
	ss_info.port = ss_port;

	memcpy(reg_msg.payload, &ss_info, sizeof(SSRegisterInfo));
	send(nm_sock, &reg_msg, sizeof(reg_msg), 0);

	// Wait for acknowledgment
	Message ack_msg;
	int bytes_read = recv(nm_sock, &ack_msg, sizeof(ack_msg), 0);
	if (bytes_read > 0 && ack_msg.type == MSG_REGISTER_ACK) {
		printf("Registered with Naming Server\n");
	} else {
		printf("Failed to register with Naming Server\n");
		close(nm_sock);
		return -1;
	}

	// Send file list to naming server
    send_file_list(nm_ip, NM_PORT, ss_info);

	close(nm_sock);

	// Start listening for client connections
	int server_fd, *new_sock;
	struct sockaddr_in address;
	int addrlen = sizeof(address);

	pthread_t thread_id;

	// Create server socket
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		perror("Server socket failed");
		exit(EXIT_FAILURE);
	}

	// Bind to specified port
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY; // Listens on all interfaces
	address.sin_port = htons(ss_port);

	if (bind(server_fd, (struct sockaddr *)&address,
			 sizeof(address)) < 0) {
		perror("Bind failed");
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	// Listen for client connections
	if (listen(server_fd, 10) < 0) {
		perror("Listen failed");
		close(server_fd);
		exit(EXIT_FAILURE);
	}

	printf("Storage Server listening on port %d...\n", ss_port);

	while (1) {
		new_sock = malloc(sizeof(int));
		*new_sock = accept(server_fd, (struct sockaddr *)&address,
						   (socklen_t *)&addrlen);
		if (*new_sock < 0) {
			perror("Accept failed");
			free(new_sock);
			continue;
		}

		// Handle client in new thread
		if (pthread_create(&thread_id, NULL, handle_client,
						   (void *)new_sock) < 0) {
			perror("Could not create thread");
			free(new_sock);
		}
		pthread_detach(thread_id);
	}

	close(server_fd);
	return 0;
}