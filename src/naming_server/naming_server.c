// naming_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "../common/protocol.h"
#define PORT 9000
#define MAX_SS 100
#define MAX_FILES 1000

typedef struct {
    char path[MAX_PATH_LENGTH];
    StorageServerInfo ss_info;
} FileInfo;

StorageServerInfo storage_servers[MAX_SS];
int ss_count = 0;

FileInfo file_info_list[MAX_FILES];
int file_count = 0;

pthread_mutex_t ss_mutex;
pthread_mutex_t file_mutex;

void add_file_info(const char *path, StorageServerInfo ss_info) {
    pthread_mutex_lock(&file_mutex);
    // Check if file already exists
    for (int i = 0; i < file_count; i++) {
        if (strcmp(file_info_list[i].path, path) == 0) {
            // Update storage server info
            file_info_list[i].ss_info = ss_info;
            pthread_mutex_unlock(&file_mutex);
            return;
        }
    }
    // Add new file info
    strcpy(file_info_list[file_count].path, path);
    file_info_list[file_count].ss_info = ss_info;
    file_count++;
    pthread_mutex_unlock(&file_mutex);
}

StorageServerInfo* find_storage_server(const char *path) {
    pthread_mutex_lock(&file_mutex);
    for (int i = 0; i < file_count; i++) {
        if (strcmp(file_info_list[i].path, path) == 0) {
            pthread_mutex_unlock(&file_mutex);
            return &file_info_list[i].ss_info;
        }
    }
    pthread_mutex_unlock(&file_mutex);
    return NULL;
}

void *handle_connection(void *arg)
{
	int client_sock = *(int *)arg;
	free(arg);
	Message msg;
	int bytes_read = recv(client_sock, &msg, sizeof(msg), 0);
	if (bytes_read <= 0)
	{
		close(client_sock);
		pthread_exit(NULL);
	}
	if (msg.type == MSG_REGISTER_SS)
	{
		// Handle Storage Server registration
		SSRegisterInfo ss_info;
		memcpy(&ss_info, msg.payload, sizeof(SSRegisterInfo));
		pthread_mutex_lock(&ss_mutex);
		strcpy(storage_servers[ss_count].ip_address, ss_info.ip_address);
		storage_servers[ss_count].port = ss_info.port;
		ss_count++;
		pthread_mutex_unlock(&ss_mutex);
		printf("Registered Storage Server: %s:%d\n",
			   ss_info.ip_address, ss_info.port);
		// Send acknowledgment
		Message ack;
		ack.type = MSG_REGISTER_ACK;
		send(client_sock, &ack, sizeof(ack), 0);

        // Receive file list update
        bytes_read = recv(client_sock, &msg, sizeof(msg), 0);
        if (bytes_read > 0 && msg.type == MSG_FILE_LIST_UPDATE) {
            SSFileListUpdate file_update;
            memcpy(&file_update, msg.payload, sizeof(SSFileListUpdate));

            char *token = strtok(file_update.file_paths, "\n");
            while (token != NULL) {
                add_file_info(token, ss_info);
                token = strtok(NULL, "\n");
            }
            printf("Updated file list from Storage Server %s:%d\n",
                   ss_info.ip_address, ss_info.port);
        }
	}
	else if (msg.type == MSG_CLIENT_REQUEST)
	{
		// Handle client requests
		ClientRequest client_req;
		memcpy(&client_req, msg.payload, sizeof(ClientRequest));
		printf("Received client request: %s %s\n",
			   client_req.command, client_req.path);

		Message nm_response;

        if (strcmp(client_req.command, "LIST") == 0) {
            // Aggregate list from all storage servers
            char aggregated_list[MAX_DATA_SIZE * MAX_SS] = {0};
            pthread_mutex_lock(&ss_mutex);
            for (int i = 0; i < ss_count; i++) {
                StorageServerInfo ss_info = storage_servers[i];
                // Connect to the Storage Server
                int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
                if (ss_sock < 0) {
                    perror("Socket creation error");
                    continue;
                }
                struct sockaddr_in ss_addr;
                ss_addr.sin_family = AF_INET;
                ss_addr.sin_port = htons(ss_info.port);
                if (inet_pton(AF_INET, ss_info.ip_address, &ss_addr.sin_addr) <= 0) {
                    perror("Invalid Storage Server IP");
                    close(ss_sock);
                    continue;
                }
                if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0) {
                    perror("Connection to Storage Server failed");
                    close(ss_sock);
                    continue;
                }

                // Send LIST request to Storage Server
                Message ss_msg;
                ss_msg.type = MSG_SS_REQUEST;
                SSRequest ss_req;
                strcpy(ss_req.command, "LIST");
                strcpy(ss_req.path, client_req.path);
                memcpy(ss_msg.payload, &ss_req, sizeof(SSRequest));
                send(ss_sock, &ss_msg, sizeof(ss_msg), 0);

                // Receive response from Storage Server
                Message ss_response;
                bytes_read = recv(ss_sock, &ss_response, sizeof(ss_response), 0);
                if (bytes_read > 0 && ss_response.type == MSG_SS_RESPONSE) {
                    strcat(aggregated_list, ss_response.payload);
                }
                close(ss_sock);
            }
            pthread_mutex_unlock(&ss_mutex);
            nm_response.type = MSG_SS_RESPONSE;
            strcpy(nm_response.payload, aggregated_list);
            send(client_sock, &nm_response, sizeof(nm_response), 0);
        } else if (strcmp(client_req.command, "READ") == 0 ||
                   strcmp(client_req.command, "WRITE") == 0) {
            // Locate the storage server
            StorageServerInfo *ss_info = find_storage_server(client_req.path);
            if (ss_info == NULL && strcmp(client_req.command, "WRITE") == 0) {
                // For WRITE command, if file doesn't exist, assign it to a storage server
                pthread_mutex_lock(&ss_mutex);
                if (ss_count > 0) {
                    ss_info = &storage_servers[0]; // Simple strategy: pick the first server
                    add_file_info(client_req.path, *ss_info);
                    pthread_mutex_unlock(&ss_mutex);
                } else {
                    pthread_mutex_unlock(&ss_mutex);
                    nm_response.type = MSG_ERROR;
                    strcpy(nm_response.payload, "No storage servers available");
                    send(client_sock, &nm_response, sizeof(nm_response), 0);
                    close(client_sock);
                    pthread_exit(NULL);
                }
            } else if (ss_info == NULL) {
                nm_response.type = MSG_ERROR;
                strcpy(nm_response.payload, "File not found");
                send(client_sock, &nm_response, sizeof(nm_response), 0);
                close(client_sock);
                pthread_exit(NULL);
            }

            // Forward request to the storage server
            int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (ss_sock < 0) {
                perror("Socket creation error");
                nm_response.type = MSG_ERROR;
                strcpy(nm_response.payload, "Internal server error");
                send(client_sock, &nm_response, sizeof(nm_response), 0);
                close(client_sock);
                pthread_exit(NULL);
            }
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(ss_info->port);
            if (inet_pton(AF_INET, ss_info->ip_address, &ss_addr.sin_addr) <= 0) {
                perror("Invalid Storage Server IP");
                close(ss_sock);
                nm_response.type = MSG_ERROR;
                strcpy(nm_response.payload, "Internal server error");
                send(client_sock, &nm_response, sizeof(nm_response), 0);
                close(client_sock);
                pthread_exit(NULL);
            }
            if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0) {
                perror("Connection to Storage Server failed");
                close(ss_sock);
                nm_response.type = MSG_ERROR;
                strcpy(nm_response.payload, "Storage server unavailable");
                send(client_sock, &nm_response, sizeof(nm_response), 0);
                close(client_sock);
                pthread_exit(NULL);
            }

            // Send request to Storage Server
            Message ss_msg;
            ss_msg.type = MSG_SS_REQUEST;
            SSRequest ss_req;
            strcpy(ss_req.command, client_req.command);
            strcpy(ss_req.path, client_req.path);
            strcpy(ss_req.data, client_req.data);
            memcpy(ss_msg.payload, &ss_req, sizeof(SSRequest));
            send(ss_sock, &ss_msg, sizeof(ss_msg), 0);

            // Receive response from Storage Server
            Message ss_response;
            bytes_read = recv(ss_sock, &ss_response, sizeof(ss_response), 0);
            if (bytes_read > 0) {
                // Update file mapping if it's a write command
                if (strcmp(client_req.command, "WRITE") == 0) {
                    add_file_info(client_req.path, *ss_info);
                }
                send(client_sock, &ss_response, sizeof(ss_response), 0);
            } else {
                nm_response.type = MSG_ERROR;
                strcpy(nm_response.payload, "Failed to receive response from storage server");
                send(client_sock, &nm_response, sizeof(nm_response), 0);
            }
            close(ss_sock);
        } else {
            nm_response.type = MSG_ERROR;
            strcpy(nm_response.payload, "Unknown command");
            send(client_sock, &nm_response, sizeof(nm_response), 0);
        }
	}
	else
	{
		// Unknown message type
		printf("Unknown message type received.\n");
	}
	close(client_sock);
	pthread_exit(NULL);
}

int main()
{
	int server_fd, *new_sock;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	pthread_mutex_init(&ss_mutex, NULL);
    pthread_mutex_init(&file_mutex, NULL);

	// Create socket
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		perror("Socket failed");
		exit(EXIT_FAILURE);
	}
	// Bind socket to port
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
	address.sin_port = htons(PORT);

	if (bind(server_fd, (struct sockaddr *)&address,
			 sizeof(address)) < 0)
	{
		perror("Bind failed");
		exit(EXIT_FAILURE);
	}
	// Listen for connections
	if (listen(server_fd, 10) < 0)
	{
		perror("Listen failed");
		exit(EXIT_FAILURE);
	}
	printf("Naming Server listening on port %d...\n", PORT);
	while (1)
	{
		new_sock = malloc(sizeof(int));
		*new_sock = accept(server_fd, (struct sockaddr *)&address,
						   (socklen_t *)&addrlen);
		if (*new_sock < 0)
		{
			perror("Accept failed");
			free(new_sock);
			continue;
		}
		// Handle each connection in a new thread
		pthread_t thread_id;
		if (pthread_create(&thread_id, NULL, handle_connection,
						   (void *)new_sock) < 0)
		{
			perror("Could not create thread");
			free(new_sock);
		}
		pthread_detach(thread_id);
	}
	pthread_mutex_destroy(&ss_mutex);
    pthread_mutex_destroy(&file_mutex);
	return 0;
}