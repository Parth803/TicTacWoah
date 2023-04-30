// NOTE: must use option -pthread when compiling!
#define _POSIX_C_SOURCE 200809L
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

#define QUEUE_SIZE 8
#define HOSTSIZE 100
#define PORTSIZE 10

volatile int active = 1;

void handler(int signum) {
    active = 0;
}

// set up signal handlers for primary thread
// return a mask blocking those signals for worker threads
void install_handlers(sigset_t *mask) {
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    // Set up signal handler for SIGINT
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Set up signal handler for SIGTERM
    if (sigaction(SIGTERM, &act, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGTERM);
}

// data to be sent to worker threads
typedef struct connection_data {
	struct sockaddr_storage addr;
	socklen_t addr_len;
	int fd;
} connection_data_t;

int open_listener(char *portNumber, int queue_size) {
    struct addrinfo hint, *info_list, *info;
    int error, sock;

    // initialize hints
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family   = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags    = AI_PASSIVE;

    // obtain information for listening socket
    error = getaddrinfo(NULL, portNumber, &hint, &info_list);
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }

    // attempt to create socket
    for (info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

        // if we could not create the socket, try the next method
        if (sock == -1) continue;

        // bind socket to requested port
        error = bind(sock, info->ai_addr, info->ai_addrlen);
        if (error) {
            close(sock);
            continue;
        }

        // enable listening for incoming connection requests
        error = listen(sock, queue_size);
        if (error) {
            close(sock);
            continue;
        }

        // if we got this far, we have opened the socket
        break;
    }

    freeaddrinfo(info_list);

    // info will be NULL if no method succeeded
    if (info == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    return sock;
}

// struct to represent a game between player x and player o with a pointer to the next node in the games_list
typedef struct game {
    char xName[128];
    int xfd;
    char oName[128];
    int ofd;
    struct game *next;
} game_t;

// handles locking and unlocking for games_list
pthread_mutex_t games_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// linked list that maintains the number of games that are active or waiting for another player.
game_t* games_list = NULL;

// adds a client to a game or create a new game if all are full (returns 1 to show that a game is full and ready to be started )
game_t* add_client_to_game(int fd, char *name) {
    // Lock the mutex before modifying games_list
    pthread_mutex_lock(&games_list_mutex); 

    // check if there is a game with an empty spot for O since all games will have an X
    game_t *curr_game_p = games_list;
    while (curr_game_p != NULL) {
        if ((curr_game_p->xfd != -1 && curr_game_p->ofd == -1)) {
            strcpy(curr_game_p->oName, name);
            curr_game_p->ofd = fd;
            // Unlock the mutex after modifying games_list
            pthread_mutex_unlock(&games_list_mutex);
            return curr_game_p;
        }
        curr_game_p = curr_game_p->next;
    }
    // no game with empty spot so create a new game
    game_t *new_game = malloc(sizeof(game_t));
    strcpy(new_game->xName, name);
    new_game->xfd = fd;
    strcpy(new_game->oName, "");
    new_game->ofd = -1;
    new_game->next = NULL;

    // add game to games_list (if non empty add to front)
    if (games_list != NULL) {
        new_game->next = games_list;
        games_list = new_game;
    }
    else {
        games_list = new_game;
    }
    // Unlock the mutex after modifying games_list
    pthread_mutex_unlock(&games_list_mutex);
    return new_game;
}

// removes game from games_list and closes connections
void scrap_game(game_t* game_to_delete) {
    // Lock the mutex before modifying games_list
    pthread_mutex_lock(&games_list_mutex); 
    game_t *current = games_list;
    game_t *previous = NULL;

    while (current != NULL) {
        if (current == game_to_delete) {  // if the current node matches the node to delete
            if (previous == NULL) {
                // if the node to delete is the head node
                games_list = current->next;
            } else {
                // if the node to delete is not the head node
                previous->next = current->next;
            }
            printf("[SERVER SCRAPPED GAME between %d: %s and %d: %s]", current->xfd, current->xName, current->ofd, current->oName);
            fflush(stdout);
            // close the sockets associated with the clients and free the memory of the node
            close(current->xfd);
            close(current->ofd);
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }
    // Unlock the mutex after modifying games_list
    pthread_mutex_unlock(&games_list_mutex);
}

// checks if a name already exists in the list (1 if it is, else 0)
int is_name_in_use(char *name) {
    // Lock the mutex before modifying games_list
    pthread_mutex_lock(&games_list_mutex); 
    game_t *curr_game_p = games_list;
    while (curr_game_p != NULL) {
        if ((strcmp(curr_game_p->xName, name) == 0) || (strcmp(curr_game_p->oName, name) == 0)) {
            // Unlock the mutex after modifying games_list
            pthread_mutex_unlock(&games_list_mutex);
            return 1;
        }
        curr_game_p = curr_game_p->next;
    }
    // Unlock the mutex after modifying games_list
    pthread_mutex_unlock(&games_list_mutex);
    return 0;
}

// checks if a move is valid (1 if yes, else 0)
int check_if_valid_move(char board[3][3], int row, int col) {
    // Check if the row and column are within the bounds of the board
    if (row < 1 || row > 3 || col < 1 || col > 3) {
        return 0;
    }
    // Check if the spot on the board is already taken
    if (board[row - 1][col - 1] != '.') {
        return 0;
    }
    return 1;
}

// checks if a player has won the game (1 if yes, else 0)
int check_win(char board[3][3], char player_role) {
    for (int i = 0; i < 3; i++) {
        // check rows
        if (board[i][0] == player_role && board[i][1] == player_role && board[i][2] == player_role) {
            return 1;
        }
        // check columns
        if (board[0][i] == player_role && board[1][i] == player_role && board[2][i] == player_role) {
            return 1;
        }
    }
    // check diagonals
    if (board[0][0] == player_role && board[1][1] == player_role && board[2][2] == player_role) {
        return 1;
    }
    if (board[0][2] == player_role && board[1][1] == player_role && board[2][0] == player_role) {
        return 1;
    }
    return 0;
}

// checks if there is a tie (1 if yes, else 0)
int check_tie(char board[3][3]) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (board[i][j] == '.')
                return 0;
        }
    }
    return 1;
}

// converts the board into the string version for when we send it to client
void format_board(char board[3][3], char* board_str) {
    int index = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            board_str[index++] = board[i][j];
        }
    }
    board_str[index] = '\0';
}

// makes move for the moving player (returns 1 if move is done, 0 if move must be redone, -1 if game is to be scrapped)
int make_move(game_t* original_game_p, game_t* curr_game_p, char board[3][3], char* board_str, char* role, messageBuffer_t* m_msgBuffer_p, message_t * m_msg_p, messageBuffer_t* w_msgBuffer_p, message_t * w_msg_p) {
    // handles connection lost error
    if (!is_socket_connected(m_msgBuffer_p->fd)) {
        // handle error caused by client socket not being connected anymore
        return 0;
    }
    // read message from moving player and check if its malformed
    if (recieve_msg(m_msgBuffer_p, m_msg_p) == -1) {
        // message recieved is malformed, as stated by prof we must send invalid and scrap the game
        set_message_fields(m_msg_p, 7, "malformed message or connection lost", NULL);
        set_message_fields(w_msg_p, 7, "malformed message or connection lost", NULL);
        send_msg(m_msgBuffer_p->fd, m_msg_p, NULL);
        send_msg(w_msgBuffer_p->fd, w_msg_p, NULL);
        scrap_game(original_game_p);
        return -1;
    }
    // check if MOVE msg
    if (m_msg_p->code == 1) {
        // check if the MOVE msg is for role
        if (strcmp(m_msg_p->thirdField, role) == 0) {
            // get the location of the move (we can use index 0 and 2 of the fourthField safely since protocol ensures that in a move message, the fourth field is formatted to be location,location)
            int row = m_msg_p->fourthField[0] - '0';
            int col = m_msg_p->fourthField[2] - '0';
            // check if the move is valid
            if (check_if_valid_move(board, row, col) == 1) {
                board[row - 1][col - 1] = *role;
                format_board(board, board_str);
                char position[BUFFER_SIZE];
                strcpy(position, m_msg_p->fourthField);
                // send MOVD message to both client m and client w with the board
                set_message_fields(m_msg_p, 6, role, position);
                set_message_fields(w_msg_p, 6, role, position);
                if ((send_msg(m_msgBuffer_p->fd, m_msg_p, board_str) == -1) || (send_msg(w_msgBuffer_p->fd, w_msg_p, board_str) == -1)) {
                    // couldn't write message, scrap the game
                    scrap_game(original_game_p);
                    return -1;
                }
                // check if the move causes a win or a tie
                if (check_win(board, *role) == 1) {
                    // game is over send W to m and L to w
                    set_message_fields(m_msg_p, 8, "W", "you have won.");
                    send_msg(m_msgBuffer_p->fd, m_msg_p, NULL);

                    char fullmsg[BUFFER_SIZE];
                    memset(fullmsg, '\0', BUFFER_SIZE);
                    if (*role == 'X') {
                        strcat(fullmsg, curr_game_p->xName);
                    }
                    else {
                        strcat(fullmsg, curr_game_p->oName);
                    }
                    strcat(fullmsg, " has completed a line and won.");
                    set_message_fields(w_msg_p, 8, "L", fullmsg);
                    send_msg(w_msgBuffer_p->fd, w_msg_p, NULL);
                    scrap_game(original_game_p);
                    return -1;
                }
                if (check_tie(board) == 1) {
                    // game is over due to tie send D to m and w
                    set_message_fields(m_msg_p, 8, "D", "the grid is full.");
                    send_msg(m_msgBuffer_p->fd, m_msg_p, NULL);

                    set_message_fields(w_msg_p, 8, "D", "the grid is full.");
                    send_msg(w_msgBuffer_p->fd, w_msg_p, NULL);
                    scrap_game(original_game_p);
                    return -1;
                }
            }
            // move is invalid
            else {
                set_message_fields(m_msg_p, 7, "invalid move", NULL);
                if ((send_msg(m_msgBuffer_p->fd, m_msg_p, NULL) == -1)) {
                    // couldn't write message, scrap the game
                    scrap_game(original_game_p);
                    return -1;
                }
                // we must do the turn again for m
                return 0;
            }
        }
        // move message is for role w which is invalid
        else {
            set_message_fields(m_msg_p, 7, "invalid role", NULL);
            if ((send_msg(m_msgBuffer_p->fd, m_msg_p, NULL) == -1)) {
                // couldn't write message, scrap the game
                scrap_game(original_game_p);
                return -1;
            }
            // we must do the turn again for m
            return 0;
        }
    }
    // check if RSGN msg 
    else if (m_msg_p->code == 2) {
        // reply with OVER
        set_message_fields(m_msg_p, 8, "L", "you have resigned.");
        send_msg(m_msgBuffer_p->fd, m_msg_p, NULL);

        char fullmsg[BUFFER_SIZE];
        memset(fullmsg, '\0', BUFFER_SIZE);
        if (*role == 'X') {
            strcat(fullmsg, curr_game_p->xName);
        }
        else {
            strcat(fullmsg, curr_game_p->oName);
        }
        strcat(fullmsg, " has resigned.");
        set_message_fields(w_msg_p, 8, "W", fullmsg);
        send_msg(w_msgBuffer_p->fd, w_msg_p, NULL);
        scrap_game(original_game_p);
        return -1;
    }
    // check if DRAW msg
    else if (m_msg_p->code == 3) {
        // check if client m wants to suggest a draw
        if (strcmp(m_msg_p->thirdField, "S") == 0) {
            // get accept or decline message from client w, looping incase they send something else since we would need to re-ask
            while (1) {
                // send draw s to client w
                set_message_fields(w_msg_p, 3, "S", NULL);
                if (send_msg(w_msgBuffer_p->fd, w_msg_p, NULL) == -1) {
                    // couldn't write message, scrap the game
                    scrap_game(original_game_p);
                    return -1;
                }
                // get message from client w
                // handles connection lost error
                if (!is_socket_connected(w_msgBuffer_p->fd)) {
                    // handle error caused by client socket not being connected anymore
                    return 0;
                }
                if (recieve_msg(w_msgBuffer_p, w_msg_p) == -1) {
                    // message recieved is malformed, as stated by prof we must send invalid and scrap the game
                    set_message_fields(m_msg_p, 7, "malformed message or connection lost", NULL);
                    set_message_fields(w_msg_p, 7, "malformed message or connection lost", NULL);
                    send_msg(m_msgBuffer_p->fd, m_msg_p, NULL);
                    send_msg(w_msgBuffer_p->fd, w_msg_p, NULL);
                    scrap_game(original_game_p);
                    return -1;
                }
                // check if we get back a valid draw reply message from client w so we can stop asking them
                if (w_msg_p->code == 3 && (strcmp(w_msg_p->thirdField, "A") == 0 || strcmp(w_msg_p->thirdField,"R") == 0)) {
                    break;
                }
                // client w didnt sent a valid reply (DRAW A or DRAW R) to DRAW S, run loop again to ask them for an accept or decline
            }
            // we now have an accept or decline message from client w so check if draw was accepted
            if (strcmp(w_msg_p->thirdField,"A") == 0) {
                // send over to both players with outcome as draw
                set_message_fields(m_msg_p, 8, "D", "both players agreed to a draw");
                set_message_fields(w_msg_p, 8, "D", "both players agreed to a draw");
                send_msg(m_msgBuffer_p->fd, m_msg_p, NULL);
                send_msg(w_msgBuffer_p->fd, w_msg_p, NULL);
                scrap_game(original_game_p);
                return -1;
            }
            // otherwise draw was declined
            else {
                // redo the turn for m since client w reject their draw proposal
                set_message_fields(m_msg_p, 3, "R", NULL);
                if ((send_msg(m_msgBuffer_p->fd, m_msg_p, NULL) == -1)) {
                    // couldn't write message, scrap the game
                    scrap_game(original_game_p);
                    return -1;
                }
                // we must do the turn again for m
                return 0;
            }
        }
        // client m sent a draw message without an "S" which is invalid.
        else {
            set_message_fields(m_msg_p, 7, "invalid field", NULL);
            if ((send_msg(m_msgBuffer_p->fd, m_msg_p, NULL) == -1)) {
                // couldn't write message, scrap the game
                scrap_game(original_game_p);
                return -1;
            }
            // we must do the turn again for m
            return 0;
        }
    }
    // any other msg code is invalid for a client to send
    else {
        // send invalid and redo turn, takes care of if client sends PLAY or any of the server msg codes
        set_message_fields(m_msg_p, 7, "invalid command", NULL);
        if ((send_msg(m_msgBuffer_p->fd, m_msg_p, NULL) == -1)) {
            // couldn't write message, scrap the game
            scrap_game(original_game_p);
            return -1;
        }
        // we must do the turn again for m
        return 0;
    }
    return 1;
}

// starts a game between two connected players
void* start_game(void* game_to_start) {
    // copy the game so that any changes to original object don't affect the game
    game_t* curr_game_p = malloc(sizeof(game_t));
    memcpy(curr_game_p, (game_t*) game_to_start, sizeof(game_t));

    printf("TIME TO PLAY! X: %s vs O: %s\n", curr_game_p->xName, curr_game_p->oName);
    fflush(stdout);

    // create objects that will buffer messages and store them for client x
    messageBuffer_t *x_msgBuffer_p = malloc(sizeof(messageBuffer_t));
    x_msgBuffer_p->fd = curr_game_p->xfd;
    x_msgBuffer_p->buflen = 0;
    memset(x_msgBuffer_p->buffer, '\0', BUFFER_SIZE);
    message_t *x_msg_p = malloc(sizeof(message_t));

    // create objects that will buffer messages and store them for client o
    messageBuffer_t *o_msgBuffer_p = malloc(sizeof(messageBuffer_t));
    o_msgBuffer_p->fd = curr_game_p->ofd;
    o_msgBuffer_p->buflen = 0;
    memset(o_msgBuffer_p->buffer, '\0', BUFFER_SIZE);
    message_t *o_msg_p = malloc(sizeof(message_t));

    // set a timeout value for both sockets when we are trying to read
    struct timeval timeout;
    timeout.tv_sec = 10; // 10 seconds
    timeout.tv_usec = 0;
    if ((setsockopt(curr_game_p->xfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) || (setsockopt(curr_game_p->ofd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)) {
        perror("setsockopt failed");
    }

    // send the begin message to x and o
    set_message_fields(x_msg_p, 5, "X", curr_game_p->oName);
    set_message_fields(o_msg_p, 5, "O", curr_game_p->xName);
    if ((send_msg(curr_game_p->xfd, x_msg_p, NULL) == -1) || (send_msg(curr_game_p->ofd, o_msg_p, NULL) == -1)) {
        // couldn't write message, scrap the game
        scrap_game(game_to_start);
        return NULL;
    }
    
    // create empty TicTacToe board and board string
    char board[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            board[i][j] = '.';
        }
    }
    char board_str[10];
    format_board(board, board_str);
    
    int gameover = 0;
    int is_x_turn = 1;
    while (!gameover) {
        // check if it's player X's turn
        if (is_x_turn) {
            int move_result = make_move(game_to_start, curr_game_p, board, board_str, "X", x_msgBuffer_p, x_msg_p, o_msgBuffer_p, o_msg_p);
            if (move_result == -1) {
                // game is over or scrapped
                break;
            } 
            else if (move_result == 1) {
                // move completed switch
                is_x_turn = 0;
            }
            else {
                // redo move
                is_x_turn = 1;
            }
        }
        else {
            // player O's turn
            int move_result = make_move(game_to_start, curr_game_p, board, board_str, "O", o_msgBuffer_p, o_msg_p, x_msgBuffer_p, x_msg_p);
            if (move_result == -1) {
                // game is over or scrapped
                break;
            } 
            else if (move_result == 1) {
                // move completed switch
                is_x_turn = 1;
            }
            else {
                // redo move
                is_x_turn = 0;
            }
        }
    }

    // clean up malloced memory
    free(curr_game_p);
    free(x_msgBuffer_p);
    free(x_msg_p);
    free(o_msgBuffer_p);
    free(o_msg_p);

    return NULL;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    sigset_t mask;
    connection_data_t *con;
    int error;
    pthread_t tid;

    char* portNumber = argc == 2 ? argv[1] : "15000";

	install_handlers(&mask);
	
    int listener = open_listener(portNumber, QUEUE_SIZE);
    if (listener < 0) exit(EXIT_FAILURE);
    
    printf("Listening for incoming connections on %s\n", portNumber);

    while (active) {
    	con = (connection_data_t *) malloc(sizeof(connection_data_t));
    	con->addr_len = sizeof(struct sockaddr_storage);
        con->fd = accept(listener, (struct sockaddr *)&con->addr, &con->addr_len);
        if (con->fd < 0) {
            perror("accept");
            free(con);
            // TODO check for specific error conditions
            continue;
        }
        
        // temporarily disable signals
        // (the worker thread will inherit this mask, ensuring that SIGINT is
        // only delivered to this thread)
        error = pthread_sigmask(SIG_BLOCK, &mask, NULL);
        if (error != 0) {
        	fprintf(stderr, "sigmask: %s\n", strerror(error));
        	exit(EXIT_FAILURE);
        }

        char host[HOSTSIZE], port[PORTSIZE];

        int error = getnameinfo((struct sockaddr *)&con->addr, con->addr_len, host, HOSTSIZE, port, PORTSIZE, NI_NUMERICSERV);
        
        if (error) {
            fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
            strcpy(host, "??");
            strcpy(port, "??");
        }

        printf("Connection from %s:%s\n", host, port);
        
        messageBuffer_t myMessageBuffer;
        myMessageBuffer.fd = con->fd;
        myMessageBuffer.buflen = 0;
        message_t myMessage;
        
        // we dont have to worry about a timeout or anything because professor mentioned in a dicussion post (https://rutgers.instructure.com/courses/210688/discussion_topics/2885093) 
        // "Yes. There is no reason for a client to delay sending PLAY after establishing a connection."
        // handles connection lost error
        if (!is_socket_connected(con->fd)) {
            // handle error caused by client socket not being connected anymore
            close(con->fd);
        }
        if (recieve_msg(&myMessageBuffer, &myMessage) == -1) {
            set_message_fields(&myMessage, 7, "malformed message or connection lost", NULL);
            if (send_msg(con->fd, &myMessage, NULL) == -1) {
                // couldn't write message, close the socket with the client
                close(con->fd);
            }
        }
        // check if the first message is a play message
        if (myMessage.code == 0) {
            char* name = strdup(myMessage.thirdField);
            // check if name is too long or too short
            if (strlen(name) > 128 || strlen(name) < 1) {
                set_message_fields(&myMessage, 7, "name is invalid", NULL);
                if (send_msg(con->fd, &myMessage, NULL) == -1) {
                    // couldn't write message, close the socket with the client
                    close(con->fd);
                }
            }
            // check if name is already in use
            else if (is_name_in_use(name) == 1) {
                set_message_fields(&myMessage, 7, "name is in use", NULL);
                if (send_msg(con->fd, &myMessage, NULL) == -1) {
                    // couldn't write message, close the socket with the client
                    close(con->fd);
                }
            }
            // the client's name is acceptable
            else {
                // send a wait and check if there is another client waiting so we can start a game
                set_message_fields(&myMessage, 4, NULL, NULL);
                if (send_msg(con->fd, &myMessage, NULL) == -1) {
                    // couldn't write message, close the socket with the client
                    close(con->fd);
                }
                // add client to a game if another client is already waiting or create a game if no other client is waiting
                game_t* game_p = add_client_to_game(con->fd, name);
                if (game_p->xfd != -1 && game_p->ofd != -1) {
                    // x and o are both connected we can start a game
                    if (is_socket_connected(game_p->xfd) && is_socket_connected(game_p->ofd)) {
                        // start a new thread to handle the game
                        int ret = pthread_create(&tid, NULL, start_game, game_p);
                        if (ret != 0) {
                            // thread couldn't be created, scrap the game and the connections
                            perror("pthread_create");
                            scrap_game(game_p);
                        }
                        // automatically clean up child threads once they terminate
                        pthread_detach(tid);
                    } 
                    // x is not connected, make o the x client and wait for a different client to become the o
                    else if (!is_socket_connected(game_p->xfd)) {
                        // Lock the mutex before modifying games_list
                        pthread_mutex_lock(&games_list_mutex); 
                        strcpy(game_p->xName, game_p->oName);
                        game_p->xfd = game_p->ofd;
                        strcpy(game_p->oName, "");
                        game_p->ofd = -1;
                        // Unlock the mutex after modifying games_list
                        pthread_mutex_unlock(&games_list_mutex);
                    }
                    // o is not connected, wait for a different client to become the o
                    else if (!is_socket_connected(game_p->ofd)) {
                        // Lock the mutex before modifying games_list
                        pthread_mutex_lock(&games_list_mutex); 
                        strcpy(game_p->oName, "");
                        game_p->ofd = -1;
                        // Unlock the mutex after modifying games_list
                        pthread_mutex_unlock(&games_list_mutex);
                    }
                    // both o and x are not connected, scrap game 
                    else {
                        scrap_game(game_p);
                    }
                }
                // do nothing as we must wait for a game to be filled to start it
                else {
                    printf("MUST WAIT TO START A GAME!\n");
                    fflush(stdout);
                }
            }
        }
        // first message is not play message so it is invalid
        else {
            set_message_fields(&myMessage, 7, "invalid message", NULL);
            if (send_msg(con->fd, &myMessage, NULL) == -1) {
                // couldn't write message, close the socket with the client
                close(con->fd);
            }
        }
        
        // unblock handled signals
        error = pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
        if (error != 0) {
        	fprintf(stderr, "sigmask: %s\n", strerror(error));
        	exit(EXIT_FAILURE);
        }
    }
    free(con);
    puts("Shutting down");
    close(listener);
    
    // returning from main() (or calling exit()) immediately terminates all
    // remaining threads

    // to allow threads to run to completion, we can terminate the primary thread
    // without calling exit() or returning from main:
    pthread_exit(NULL);
    // child threads will terminate once they check the value of active, but
    // there is a risk that read() will block indefinitely, preventing the
    // thread (and process) from terminating
    
	// to get a timely shut-down of all threads, including those blocked by
	// read(), we will could maintain a global list of all active thread IDs
	// and use pthread_cancel() or pthread_kill() to wake each one

    return EXIT_SUCCESS;
}
