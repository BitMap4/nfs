// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../common/protocol.h"

#define MAX_INPUT_SIZE 1024

#define UPPER(c) ((c >= 'a' && c <= 'z') ? c - 32 : c)

int execute_command(const char *nm_ip, int nm_port, char *command, char *path, char *data) {
    // Connect to Naming Server
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0) {
        perror("Socket creation error");
        return -1;
    }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);

    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("Invalid Naming Server IP");
        close(nm_sock);
        return -1;
    }

    if (connect(nm_sock, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to Naming Server failed");
        close(nm_sock);
        return -1;
    }

    // Send request to Naming Server
    Message client_msg;
    client_msg.type = MSG_CLIENT_REQUEST;
    ClientRequest client_req;
    strcpy(client_req.command, command);
    strcpy(client_req.path, path);
    if (strcmp(command, "WRITE") == 0 && data != NULL) {
        strcpy(client_req.data, data);
    } else {
        memset(client_req.data, 0, sizeof(client_req.data));
    }
    memcpy(client_msg.payload, &client_req, sizeof(ClientRequest));
    send(nm_sock, &client_msg, sizeof(client_msg), 0);

    // Receive response from Naming Server
    Message nm_response;
    int bytes_read = recv(nm_sock, &nm_response, sizeof(nm_response), 0);
    if (bytes_read <= 0) {
        printf("Failed to receive response from Naming Server\n");
        close(nm_sock);
        return -1;
    }

    if (nm_response.type == MSG_SS_RESPONSE) {
        // Operation successful, print response
        printf("%s", nm_response.payload);
    } else if (nm_response.type == MSG_ERROR) {
        printf("Error: %s\n", nm_response.payload);
    }

    close(nm_sock);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <NM_IP> <NM_Port>\n", argv[0]);
        return -1;
    }

    char *nm_ip = argv[1];
    int nm_port = atoi(argv[2]);

    char input[MAX_INPUT_SIZE];
    char command[256], path[512], data[1024];

    while (1) {
        printf("nfs> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;

        // Parse input
        memset(command, 0, sizeof(command));
        memset(path, 0, sizeof(path));
        memset(data, 0, sizeof(data));

        int args = sscanf(input, "%s %s %[^\n]", command, path, data);
        if (args < 1) {
            continue;
        }

        // Convert command to uppercase
        for (int i = 0; command[i]; i++) {
            command[i] = UPPER(command[i]);
        }

        if (strcmp(command, "EXIT") == 0 || strcmp(command, "QUIT") == 0) {
            break;
        }

        if ((strcmp(command, "READ") != 0 && strcmp(command, "WRITE") != 0 && strcmp(command, "LIST") != 0) || 
            (strcmp(command, "WRITE") == 0 && args < 3)) {
            printf("Invalid command or missing arguments.\n");
            continue;
        }

        if (strcmp(command, "WRITE") == 0 && args < 3) {
            printf("Enter data to write: ");
            if (!fgets(data, sizeof(data), stdin)) {
                printf("Failed to read data.\n");
                continue;
            }
            // Remove trailing newline
            data[strcspn(data, "\n")] = 0;
        }

        execute_command(nm_ip, nm_port, command, path, (strcmp(command, "WRITE") == 0) ? data : NULL);
    }

    return 0;
}