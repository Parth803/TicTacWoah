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

#define BOARD_SIZE 3
#define BUFFER_SIZE 256

// checks if a player has won the game (1 if yes, else 0)
int check_win(char board[3][3], char player_role)
{
    for (int i = 0; i < 3; i++)
    {
        // check rows
        if (board[i][0] == player_role && board[i][1] == player_role && board[i][2] == player_role)
        {
            return 1;
        }
        // check columns
        if (board[0][i] == player_role && board[1][i] == player_role && board[2][i] == player_role)
        {
            return 1;
        }
    }
    // check diagonals
    if (board[0][0] == player_role && board[1][1] == player_role && board[2][2] == player_role)
    {
        return 1;
    }
    if (board[0][2] == player_role && board[1][1] == player_role && board[2][0] == player_role)
    {
        return 1;
    }
    return 0;
}

// checks if there is a tie (1 if yes, else 0)
int check_tie(char board[3][3])
{
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            if (board[i][j] == '.')
                return 0;
        }
    }
    return 1;
}

void send_resign(int sockfd)
{
    char message[BUFFER_SIZE];
    int message_length = snprintf(message, sizeof(message), "RSGN");

    char formatted_message[BUFFER_SIZE];
    snprintf(formatted_message, sizeof(formatted_message), "RSGN|0|");

    write(sockfd, formatted_message, strlen(formatted_message));
    printf("MESSAGE SENT: %s\n", formatted_message);
}

void send_draw(int sockfd, char draw_type)
{
    char message[BUFFER_SIZE];
    int message_length = snprintf(message, sizeof(message), "DRAW|%c|", draw_type);

    char formatted_message[BUFFER_SIZE];
    snprintf(formatted_message, sizeof(formatted_message), "DRAW|%d|%c|", message_length - 5, draw_type);

    write(sockfd, formatted_message, strlen(formatted_message));
    printf("MESSAGE SENT: %s\n", formatted_message);
}

void send_move(int sockfd, char role, const char *user_input)
{
    char message[BUFFER_SIZE];
    int message_length = snprintf(message, sizeof(message), "MOVE|%c|%s|", role, user_input);
    char formatted_message[BUFFER_SIZE];
    snprintf(formatted_message, sizeof(formatted_message), "MOVE|%d|%c|%s|", message_length - 5, role, user_input);

    write(sockfd, formatted_message, strlen(formatted_message));
    printf("MESSAGE SENT: %s\n", formatted_message);
}

void display_board(char board[BOARD_SIZE][BOARD_SIZE])
{
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        for (int j = 0; j < BOARD_SIZE; j++)
        {
            printf("%c ", board[i][j]);
            if (j < BOARD_SIZE - 1)
            {
                printf("| ");
            }
        }
        printf("\n");
        if (i < BOARD_SIZE - 1)
        {
            printf("---------\n");
        }
    }
}

void print_prompt(char role, int received_draw)
{
    printf("\n%c's turn. Options:\n", role);
    printf("1. MOVE|row,col\n");
    printf("2. RSGN\n");
    if (received_draw)
    {
        printf("3. DRAW R\n");
        printf("4. DRAW A\n");
    }
    else
    {
        printf("3. DRAW\n");
    }
}

int connect_inet(char *host, char *service)
{
    struct addrinfo hints, *info_list, *info;
    int sock, error;

    // look up remote host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // in practice, this means give us IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // indicate we want a streaming socket

    error = getaddrinfo(host, service, &hints, &info_list);
    if (error)
    {
        fprintf(stderr, "error looking up %s:%s: %s\n", host, service, gai_strerror(error));
        return -1;
    }

    for (info = info_list; info != NULL; info = info->ai_next)
    {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (sock < 0)
            continue;

        error = connect(sock, info->ai_addr, info->ai_addrlen);
        if (error)
        {
            close(sock);
            continue;
        }

        break;
    }
    freeaddrinfo(info_list);

    if (info == NULL)
    {
        fprintf(stderr, "Unable to connect to %s:%s\n", host, service);
        return -1;
    }

    return sock;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Provide Domain Name and Port Number.");
        printf("Usage: ./ttt <domain name> <port number>\n");
        return EXIT_FAILURE;
    }

    // Get the domain name and port number of the desired service.
    char *domainName = argv[1];
    char *portNumber = argv[2];
    printf("DOMAIN: %s\nPORT: %s\n", domainName, portNumber);

    int sockfd;
    struct sockaddr_in servaddr;

    // Create socket
    sockfd = connect_inet(domainName, portNumber);
    if (sockfd < 0)
    {
        return EXIT_FAILURE;
    }
    // Ask user for their name
    printf("Enter your name: ");
    fflush(stdout);

    // send name/play to server
    char name[128];
    ssize_t nbytes = read(STDIN_FILENO, name, sizeof(name));
    name[strcspn(name, "\n")] = 0; // Remove newline character
    name[128] = '\0';
    char name_msg[BUFFER_SIZE];
    snprintf(name_msg, sizeof(name_msg), "PLAY|%zu|%s|", strlen(name) + 1, name);
    write(sockfd, name_msg, strlen(name_msg));

    // Wait for server's response
    char buffer[BUFFER_SIZE];
    read(sockfd, buffer, sizeof(buffer));
    printf("MESSAGE RECIEVED: %s\n", buffer);
    fflush(stdout);
    if (strstr(buffer, "INVL") != NULL)
    {
        char *reason;
        int i = 0;
        // get player name from play message
        reason = strtok(buffer, "|");
        while (reason != NULL)
        {
            if (i == 2)
            {
                printf("Invalid name: %s\n", reason);
                exit(0);
            }
            reason = strtok(NULL, "|");
            i++;
        }
    }
    else
    {
        memset(buffer, '\0', sizeof(buffer));
    }
    // Wait for the BEGN message and read from server
    read(sockfd, buffer, sizeof(buffer));
    if (strstr(buffer, "BEGN") == NULL)
    {
        return 0;
    }
    printf("MESSAGE RECIEVED: %s\n", buffer);
    fflush(stdout);
    // Extract role and opponent's name
    int size;
    char role, opponent_name[128];
    sscanf(buffer, "BEGN|%d|%c|%[^|]|", &size, &role, opponent_name);
    printf("Your role: %c\nOpponent's Name: %s\n", role, opponent_name);

    int received_draw = 0;
    // Game loop
    char board[BOARD_SIZE][BOARD_SIZE] = {{'.', '.', '.'}, {'.', '.', '.'}, {'.', '.', '.'}};
    int gameover = 0;
    int is_client_turn = 0;
    int draw_sent = 0;
    if (role == 'X')
    {
        is_client_turn = 1;
        display_board;
        print_prompt(role, received_draw);
        char user_input[10];
        fgets(user_input, sizeof(user_input), stdin);
        user_input[strcspn(user_input, "\n")] = 0;

        if (strcmp(user_input, "RSGN") == 0)
        {
            send_resign(sockfd);
            gameover = 1;
            read(sockfd, buffer, sizeof(buffer));
            printf("MESSAGE RECIEVED: %s\n", buffer);
        }
        else if (strncmp(user_input, "DRAW", 4) == 0)
        {
            char draw_type = user_input[5];
            send_draw(sockfd, 'S');
            draw_sent = 1;
        }
        else
        {
            send_move(sockfd, role, user_input);
        }
    }

    const char *moves_X[] = {"1,1", "1,2", "2,2", "2,1", "3,3"};
    const char *moves_O[] = {"1,3", "3,1", "2,3", "3,2"};

    int move_index_X = 0;
    int move_index_O = 0;
    int move_count = 0;
    while (!gameover)
    {

        // Receive game state from the server
        bzero(buffer, sizeof(buffer));
        read(sockfd, buffer, sizeof(buffer));
        printf("Server: %s\n", buffer);

        // Handle the server's message
        char code[5];
        sscanf(buffer, "%4s", code);
        if (strcmp(code, "MOVD") == 0)
        {

            // Update the board
            char *board_str;
            char finished_role;
            finished_role = *(buffer + 8);
            board_str = buffer + 14;
            int k = 0;
            for (int i = 0; i < BOARD_SIZE; i++)
            {
                for (int j = 0; j < BOARD_SIZE; j++)
                {
                    board[i][j] = board_str[k];
                    k++;
                }
            }
            display_board(board);
            if (finished_role == role || check_tie(board) || check_win(board, finished_role))
            {
                is_client_turn = 0;
            }
            else
            {
                is_client_turn = 1;
            }
        }
        else if (strcmp(code, "INVL") == 0)
        {

            printf("Invalid move: %s\n", buffer + 5);
        }
        else if (!draw_sent && strcmp(code, "DRAW") == 0)
        {
            received_draw = 1;
            print_prompt(role, received_draw);
            char user_input[5];

            char decision;
            printf("Do you accept the draw request? (y/n): ");
            scanf(" %c", &decision);
            getchar(); // To consume the newline character in the input buffer

            char response[7];
            if (decision == 'y' || decision == 'Y')
            {
                send_draw(sockfd, 'A');
            }
            else
            {
                send_draw(sockfd, 'R');
            }
        }
        else if (strcmp(code, "OVER") == 0)
        {
            received_draw = 0;
            printf("Game over: %s\n", buffer + 5);
            gameover = 1;
            break;
        }

        // Prompt the user for input
        if (is_client_turn)
        {
            move_count++;
            if (draw_sent) {
                draw_sent = 0;
            }
            // Send predetermined moves
            if (role == 'X' && move_index_X < 5)
            {
                if (move_count == 3)
                {
                    send_resign(sockfd);
                    gameover = 1;
                    continue;
                }

                char user_input[10];
                strcpy(user_input, moves_X[move_index_X]);
                send_move(sockfd, role, user_input);
                move_index_X++;
            }
            else if (role == 'O' && move_index_O < 4)
            {
                if (move_count == 3)
                {
                    send_resign(sockfd);
                    gameover = 1;
                    continue;
                }

                char user_input[10];
                strcpy(user_input, moves_O[move_index_O]);
                send_move(sockfd, role, user_input);
                move_index_O++;
            }
            else
            {
                printf("No more predetermined moves available.\n");
                gameover = 1;
            }
        }
    }

    close(sockfd);

    return 0;
}