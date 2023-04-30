// imported for opening txt files for testing
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
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
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    message_t myMessage;
    messageBuffer_t myMessageBuffer;
    int fd = open("input.txt", O_RDONLY);
    myMessageBuffer.fd = fd;
    myMessageBuffer.buflen = 0;
    memset(myMessageBuffer.buffer, '\0', BUFFER_SIZE);
    
    int res = recieve_msg(&myMessageBuffer, &myMessage);
    if (res > -1) {
        printf("FIRSTFIELD:%s\nSECONDFIELD:%d\nTHIRDFIELD:%s\nFOURTHFIELD:%s\n", get_message_code_string(myMessage.code), myMessage.secondField, myMessage.thirdField, myMessage.fourthField);
    }
    while (myMessageBuffer.buflen != 0) {
        res = recieve_msg(&myMessageBuffer, &myMessage);
        printf("RESULT: %d\n\n", res);
        if (res > -1) {
            printf("FIRSTFIELD:%s\nSECONDFIELD:%d\nTHIRDFIELD:%s\nFOURTHFIELD:%s\n", get_message_code_string(myMessage.code), myMessage.secondField, myMessage.thirdField, myMessage.fourthField);
        }
    }
    return 0;    
}