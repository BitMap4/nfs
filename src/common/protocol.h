// protocol.h

#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_PATH_LENGTH 256
#define MAX_COMMAND_LENGTH 16
#define MAX_DATA_SIZE 1024
#define MAX_PAYLOAD_SIZE 2048

typedef enum {
    MSG_REGISTER_SS,
    MSG_REGISTER_ACK,
    MSG_CLIENT_REQUEST,
    MSG_NM_RESPONSE,
    MSG_SS_REQUEST,
    MSG_SS_RESPONSE,
    MSG_ERROR,
    MSG_FILE_LIST_UPDATE // New message type
} MessageType;

typedef struct {
    MessageType type;
    char payload[MAX_PAYLOAD_SIZE];
} Message;

// Unified Storage Server info
typedef struct {
    char ip_address[16];
    int port;
} StorageServerInfo;

// Alias SSRegisterInfo to StorageServerInfo
typedef StorageServerInfo SSRegisterInfo;

// Storage Server file list update
typedef struct {
    int file_count;
    char file_paths[MAX_DATA_SIZE]; // A concatenated string of file paths
} SSFileListUpdate;

typedef struct {
    char command[MAX_COMMAND_LENGTH];
    char path[MAX_PATH_LENGTH];
    char data[MAX_DATA_SIZE];
} ClientRequest;

// Storage Server request structure
typedef struct {
    char command[MAX_COMMAND_LENGTH];
    char path[MAX_PATH_LENGTH];
    char data[MAX_DATA_SIZE];
} SSRequest;

#endif // PROTOCOL_H