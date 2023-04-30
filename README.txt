Parth Patel

Files included in repo
	1. ttts.c
		- main server file which contains the source code for the tictactoe online server.
	2. protocol.c
        - file that stores the implemented function used in the protocol to send and recieve messages.
    3. protocol.h
		- protocol header file, stores definitions of structs and the function headers.
	3. Makefile
		- a make file used to make it easy to compile run and test the game.
	4. test files

ttts Design:
    Main functions:
        - start_game
            - starts a game between two clients. Establishes a timeout for the connected sockets/players and uses make_move to handle player moves.
        - main
            - main function of the server, creates a listener that waiting for incoming connections, checks if they have a valid name and places them into a game object.
            - one a game object has two connected sockets/players, we start a game betweeen them in a seperate thread using start_game function.
        - games_list_mutex   
            - Locking is done using this mutex lock, so that multiple threads cannot all access the games_list at the same time, because they are blocked until a thread is done modifying it. 
            - This takes care of race conditions and ensures that access to the shared list of in-use names (in our case the games) is performed safely.
            - It also makes sure that the deletion/scrapping of a game in the linked list (games_list) is done in a safe manner so the links and a connected list is maintained.
    Helper functions:
        - the server also has many more helper functions that enable code reusability and deal with manipulating the games_list and checking for conditions in a game such as a win or tie.
	
    - more detailed comments can be found in ttts.c

Test Plan:
    - Use of running sample clients against each other to simulate a specific scenario
    - Simulates user input using variations of the original ttt.c client
    - Use of "broken" clients that intentionally send invalid or wrong messages to test if the server can handle them properly

    Test cases:
    1. tttwin.c will test victory condition
        - run tttwin.c against itself and make one move manually and it will send pre determined messages that will always end up in X winning the game.
    2. tttrsgn.c will test RSGN function
        - run tttrsgn.c against itself and it make one move, it will RSGN after the 3rd move
    3. tttbreak.c is a "broken"
        - this client sends PLAY after every message and sends MOVD instead of MOVE
        - run tttbreak.c and use manual input to try and play a game
        - this client is intended to not be able to do anything besides RSGN and DRAW and is only used for testing whether or not the server can handle invalid messages and errors
        - this client also comes with a close option that when entered, closes the socket and disonnects from the server
    4. ttt.c
        - base client that can be used for manual input
        - to send a MOVE message, the user must type input in this format: row,col. no need to write MOVE or anything else
        - to send a RSGN message, the user must input RSGN
        - to send a DRAW message, the user must input DRAW which will then ask the other user if they would agree to a draw by typing y/n. the client will then send DRAW A or DRAW R to the server depending on the answer to the prompt. it is important to know that the client is not fully capable of handling DRAW messages but the server is able to handle them properly. the proof is in the server logs where the server prints out messages sent and received.
    5. protocoltest.c
        - program that sends the messages in the input.txt file to protocol functions the first line contains all correctly formatted messaged
        - edit the input.txt to try out senarios that simulate what would happen if the server recieved that message
        - makes it easy to functionally test the protocol functions


Execution in terminal:
	1. Ensure that you are in the correct directory where the files reside
	2. Compile all the files using this command: make
	3. Run the server in a terminal using this command: make server
    4a. Run two clients in two seperate terminal using this command to manually play the game: 
        - make client
    4b. Run two clients in two seperate terminal using any of these commands to run test clients: 
        - make rsgnclient
        - make winclient
        - make breakclient
	5. Clean the environment using this command: make clean