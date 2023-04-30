#ifndef PROTOCOL_H
#define PROTOCOL_H

#define BUFFER_SIZE 1028

typedef enum {
    PLAY, // 0
    MOVE, // 1
    RSGN, // 2
    DRAW, // 3
    WAIT, // 4
    BEGN, // 5
    MOVD, // 6
    INVL, // 7
    OVER  // 8
} MessageCode;


typedef struct message {
    MessageCode code;
    int secondField;
    char thirdField[256];
    char fourthField[256];
} message_t;


typedef struct messageBuffer {
    int fd;
    char buffer[BUFFER_SIZE];
    int buflen;
} messageBuffer_t;

int is_socket_connected(int fd);
char* get_message_code_string(MessageCode code);
void set_message_fields(message_t* msg, int code, char* thirdField, char* fourthField);
int recieve_msg(messageBuffer_t* msgBuffer, message_t* msg);
int send_msg(int fd, message_t* msg, char* board_str);

#endif