#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

int is_socket_connected(int fd) {
    char buf;
    int retval = recv(fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);

    if (retval == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // socket is still connected and there is no data available to read
        return 1;
    }

    if (retval <= 0) {
        // socket is not connected
        return 0;
    }

    // success
    return 1;
}

char* get_message_code_string(MessageCode code) {
    switch (code) {
        case 0:
            return "PLAY";
        case 1:
            return "MOVE";
        case 2:
            return "RSGN";
        case 3:
            return "DRAW";
        case 4:
            return "WAIT";
        case 5:
            return "BEGN";
        case 6:
            return "MOVD";
        case 7:
            return "INVL";
        case 8:
            return "OVER";
    }
    return "";
}
// sets the fields of a message
void set_message_fields(message_t* msg, int code, char* thirdField, char* fourthField) {
    // clear out the 3rd and fourth message fields before setting the fields.
    memset(msg->thirdField, '\0', sizeof(msg->thirdField));
    memset(msg->fourthField, '\0', sizeof(msg->fourthField));

    // checks if it is 4 field message
    if (thirdField != NULL && fourthField != NULL) {
        msg->code = code;
        msg->secondField = strlen(thirdField) + strlen(fourthField) + 2;
        strcpy(msg->thirdField, thirdField);
        strcpy(msg->fourthField, fourthField);
        return;
    } 
    // checks if it is a 3 field message
    else if (thirdField != NULL && fourthField == NULL) {
        msg->code = code;
        msg->secondField = strlen(thirdField) + 1;
        strcpy(msg->thirdField, thirdField);
        return;
    } 
    // it is a 2 field message
    else {
        msg->code = code;
        msg->secondField = 0;
        return;
    }
}

// returns 1 on success, -1 if error (invalid/malformed message, signal error, connection lost error, etc.)
int recieve_msg(messageBuffer_t* msgBuffer, message_t* msg) {
    // clear out the message fields before reading.
    msg->code = 0;
    msg->secondField = 0;
    memset(msg->thirdField, '\0', sizeof(msg->thirdField));
    memset(msg->fourthField, '\0', sizeof(msg->fourthField));

    while (1) {
        int bytes_read = read(msgBuffer->fd, msgBuffer->buffer + msgBuffer->buflen, BUFFER_SIZE - msgBuffer->buflen);
        if (bytes_read < 0) {
            perror("Error reading from client");
            return -1;
        } 
        else {
            msgBuffer->buflen += bytes_read;
            msgBuffer->buffer[msgBuffer->buflen] = '\0';
            char* message_end = NULL;
            // TYPE|0| --  7 is the minimum size of any message that can possibly be sent so if buflen is smaller just 
            if (msgBuffer->buflen < 7) { 
                return -1;
            }
            // find the first '|', if there isn't one, it is a malformed msg. 
            char* type_end = strchr(msgBuffer->buffer, '|'); 
            if (type_end == NULL) {
                return -1;
            }
            // check if the code field isn't 4 bytes long implying a malformed msg.
            if ((type_end - msgBuffer->buffer) != 4) {
                return -1;
            }
            // get the bars requried by each type of message after the first two bars
            int addl_bars;
            MessageCode msgcode;
            if (strncmp(msgBuffer->buffer, "PLAY", 4) == 0) {
                addl_bars = 1;
                msgcode = PLAY;
            } else if (strncmp(msgBuffer->buffer, "DRAW", 4) == 0) {
                addl_bars = 1;
                msgcode = DRAW;
            } else if (strncmp(msgBuffer->buffer, "MOVE", 4) == 0) {
                addl_bars = 2;
                msgcode = MOVE;
            } else if (strncmp(msgBuffer->buffer, "RSGN", 4) == 0) {
                addl_bars = 0;
                msgcode = RSGN;
            } else {
                // the first 4 bytes didn't match the protocol for message type so this is a malformed msg.
                return -1;
            }
            // find the second '|', if there isn't one, it is a malformed msg. 
            char* size_end = strchr(type_end + 1, '|'); 
            if (size_end == NULL) {
                return -1;
            }
            // get the size of the data that should follow the second bar
            int dataSize = atoi(type_end + 1);
            // check if data size code is greater than 256 or less than 0 meaning malformed msg. 
            if (dataSize < 0 || dataSize > 256) {
                return -1;
            }
            // check if the message is a draw message, which would always have 2 in the second field (datasize field)
            if (msgcode == DRAW && dataSize != 2) {
                return -1;
            }
            // check if the message is a move message, which would always have 6 in the second field (datasize field)
            if (msgcode == MOVE && dataSize != 6) {
                return -1;
            }
            // check if the message was a RSGN msg, which would have a size of 0 otherwise this is a malformed RSGN msg.
            if (addl_bars == 0) {
                if (dataSize != 0) {
                    return -1;
                } 
                else {
                    message_end = size_end;
                }
            }
            else if (addl_bars == 1) {
                // find the third '|', if there isn't one, it is a malformed msg. 
                char* thirdfield_end = strchr(size_end + 1, '|'); 
                if (thirdfield_end == NULL) {
                    // check if the specified data size is <= actual size of the data after the second bar.
                    if (((msgBuffer->buffer + msgBuffer->buflen - 1) - size_end) >= dataSize) {
                        // we've read enough data (specified in the seconf field) after the second bar but we dont have a bar so this is malformed message.
                        return -1;
                    }
                    else {
                        // bars required are not good and actual data read is smaller than specified size so we must read more.
                        continue;
                    }
                }
                else {
                    // check if the specified data size is not the actual size of the data after the second bar.
                    if ((thirdfield_end - size_end) != dataSize) {
                        // bars required are good but size is not correct so this is malformed message.
                        return -1;
                    }
                    else {
                        // check if the draw message doesn't have a valid third field character S, A, or R
                        if (msgcode == DRAW && strncmp(size_end + 1, "S", 1) != 0 && strncmp(size_end + 1, "A", 1) != 0 && strncmp(size_end + 1, "R", 1) != 0) {
                            return -1;
                        }
                        // bars required are good and size is good so this is a complete message.
                        message_end = thirdfield_end;
                        // copy the contents of the third field into the message struct
                        memcpy(msg->thirdField, size_end + 1, message_end - size_end - 1);
                        msg->thirdField[message_end - size_end - 1] = '\0';
                    }
                }
            }
            // this is basically an else but i left it as else if since it's more readable
            else if (addl_bars == 2) {
                char* thirdfield_end = strchr(size_end + 1, '|');
                if (thirdfield_end == NULL) {
                    // check if the specified data size is <= actual size of the data after the second bar.
                    if (((msgBuffer->buffer + msgBuffer->buflen - 1) - size_end) >= dataSize) {
                        // we've read enough data (specified in the seconf field) after the second bar but we dont have a bar so this is malformed message.
                        return -1;
                    }
                    else {
                        // bars required are not good and actual data read is smaller than specified size so we must read more.
                        continue;
                    }
                }
                else {
                    // check if the MOVE message has only one char in the third field (player role)
                    if ((thirdfield_end - size_end != 2)) {
                        return -1;
                    }
                    // check if the MOVE message doesn't have a valid third field character (must be X or O)
                    if (strncmp(size_end + 1, "X", 1) != 0 && strncmp(size_end + 1, "O", 1) != 0) {
                        return -1;
                    }
                    char* fourthfield_end = strchr(thirdfield_end + 1, '|');
                    if (fourthfield_end == NULL) {
                        // check if the specified data size is <= actual size of the data after the second bar.
                        if (((msgBuffer->buffer + msgBuffer->buflen - 1) - size_end) >= dataSize) {
                            // we've read enough data (specified in the seconf field) after the second bar but we dont have a bar so this is malformed message.
                            return -1;
                        }
                        else {
                            // bars required are not good and actual data read is smaller than specified size so we must read more.
                            continue;
                        }
                    }
                    else {
                        // check if the specified data size is not the actual size of the data after the second bar.
                        if ((fourthfield_end - size_end) != dataSize) {
                            // bars required are good but size is not correct so this is malformed message.
                            return -1;
                        }
                        else {
                            // check if the MOVE message has only 3 char in the fourth field (position,position)
                            if ((fourthfield_end - thirdfield_end != 4)) {
                                return -1;
                            }
                            // check if the fourth field is not formatted properly to be position,position
                            if (!isdigit(*(thirdfield_end + 1)) || strncmp(thirdfield_end + 2, ",", 1) != 0 || !isdigit(*(thirdfield_end + 3))) {
                                return -1;
                            }
                            // bars required are good and size is good so this is a complete message.
                            message_end = fourthfield_end;
                            // copy the contents of the third field into the message struct
                            memcpy(msg->thirdField, size_end + 1, thirdfield_end - size_end - 1);
                            msg->thirdField[thirdfield_end - size_end - 1] = '\0';
                            
                            // copy the contents of the third field into the message struct
                            memcpy(msg->fourthField, thirdfield_end + 1, message_end - thirdfield_end - 1);
                            msg->fourthField[message_end - thirdfield_end - 1] = '\0';
                        }
                    }
                }
            }
            
            // check if we got a complete message
            if (message_end != NULL) {
                // set the values for the first two fields in message struct
                msg->code = msgcode;
                msg->secondField = dataSize;

                // if we have leftover data, move it to the front of the buffer and stop reading
                int leftover_length = msgBuffer->buflen - ((message_end + 1) - msgBuffer->buffer);
                memmove(msgBuffer->buffer, message_end + 1, leftover_length);
                memset(msgBuffer->buffer + leftover_length, '\0', BUFFER_SIZE - leftover_length); // clear remaining bytes
                msgBuffer->buflen = leftover_length;
                return 1;
            }
        }
    }
}

// returns 1 on success, -1 if error
int send_msg(int fd, message_t* msg, char* board_str) {
    // handles connection lost error (we include this check in send_msg because we dont use it with files, whereas we use recieve_msg with a text file which is not a connected socket so we do the check before every recieve_msg in the server code.)
    if (!is_socket_connected(fd)) {
        // handle error caused by client socket not being connected anymore
        return -1;
    }
    char buffer[BUFFER_SIZE];
    int len = 0;
    // format message into a string
    if (msg->thirdField[0] != '\0' && msg->fourthField[0] != '\0' && board_str != NULL) {
        len = sprintf(buffer, "%s|%d|%s|%s|%s|", get_message_code_string(msg->code), (msg->secondField + 10), msg->thirdField, msg->fourthField, board_str);
    }
    else if (msg->thirdField[0] != '\0' && msg->fourthField[0] != '\0') {
        len = sprintf(buffer, "%s|%d|%s|%s|", get_message_code_string(msg->code), msg->secondField, msg->thirdField, msg->fourthField);
    }
    else if (msg->thirdField[0] != '\0') {
        len = sprintf(buffer, "%s|%d|%s|", get_message_code_string(msg->code), msg->secondField, msg->thirdField);
    } 
    else {
        len = sprintf(buffer, "%s|%d|", get_message_code_string(msg->code), msg->secondField);
    }
    int total_written = 0;
    // write message to socket
    while (total_written < len) {
        int bytes_written = write(fd, buffer + total_written, len - total_written);
        if (bytes_written == -1) {
            perror("write");
            return -1;
        }
        total_written += bytes_written;
    }
    printf("[SERVER SEND to %d]: %s\n", fd, buffer);
    // clear out the message fields since the write was successful.
    msg->code = 0;
    msg->secondField = 0;
    memset(msg->thirdField, '\0', sizeof(msg->thirdField));
    memset(msg->fourthField, '\0', sizeof(msg->fourthField));
    return 1;
}

