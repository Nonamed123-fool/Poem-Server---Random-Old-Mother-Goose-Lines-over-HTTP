//
//  main.c
//  lab5
//
//  Created by David Sovann on 3/10/2025.
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>

#define SERVER_PORT 5280      // Port number using last 4 digits of student ID
#define BUFFER_SIZE 1024      // Buffer size for network communication

void manage_connection(int in, int out);  // Handles individual client connections
void handle_sigchld(int signo);           // Cleans up zombie child processes

/*
 * COMPLETE OLD MOTHER GOOSE POEM
 * Stored as an array of strings for easy access and random line selection
 */
const char *poem_lines[] = {
    "OLD Mother Goose, when",
    "  She wanted to wander,",
    "Would ride through the air",
    "  On a very fine gander.",
    "Mother Goose had a house,",
    "  'Twas built in a wood,",
    "Where an owl at the door",
    "  For sentinel stood.",
    "This is her son Jack,",
    "  A plain-looking lad,",
    "He is not very good,",
    "  Nor yet very bad.",
    "She sent him to market,",
    "  A live goose he bought,",
    "\"Here, mother,\" says he,",
    "  \"It will not go for nought.\"",
    "Jack's goose and her gander",
    "  Grew very fond,",
    "They'd both eat together,",
    "  Or swim in one pond.",
    "Jack found one fine morning",
    "  As I have been told,",
    "His goose had laid him",
    "  An egg of pure gold.",
    "Jack rode to his mother,",
    "  The news for to tell,",
    "She called him a good boy",
    "  And said it was well.",
    "Jack sold his gold egg",
    "  To a rogue of a Jew,",
    "Who cheated him out of",
    "  The half of his due.",
    "Then Jack went a-courting",
    "  A lady so gay,",
    "As fair as the lily,",
    "  And sweet as the May.",
    "The Jew and the Squire",
    "  Came behind his back,",
    "And began to belabour",
    "  The sides of poor Jack.",
    "And then the gold egg",
    "  Was thrown into the sea,",
    "When Jack he jumped in,",
    "  And got it back presently.",
    "The Jew got the goose,",
    "  Which he vowed he would kill,",
    "Resolving at once",
    "  His pockets to fill.",
    "Jack's mother came in,",
    "  And caught the goose soon,",
    "And mounting its back,",
    "  Flew up to the moon."
};

const int total_lines = sizeof(poem_lines) / sizeof(poem_lines[0]);  // Calculate total lines in poem

/*
 * MAIN SERVER FUNCTION
 * Sets up the server socket, handles connections, and forks child processes
 */
int main()
{
    int listen_sock, client_sock;        // Socket file descriptors
    socklen_t client_len;                // Client address length
    pid_t pid;                           // Process ID for forking
    struct sockaddr_in server, client;   // Server and client address structures
    struct hostent *client_details;      // Client host information
    struct sigaction cldsig;             // Signal handling structure

    printf("Poem Server: Starting up...\n");
    
    // SEED RANDOM NUMBER GENERATOR
    // Seed only once in main to avoid duplicate sequences in child processes
    srand(time(NULL));

    // SET UP SIGCHLD HANDLER TO PREVENT ZOMBIE PROCESSES
    // When child processes terminate, this handler automatically reaps them
    cldsig.sa_handler = handle_sigchld;   // Set the handler function
    sigemptyset(&cldsig.sa_mask);         // Clear the signal mask
    cldsig.sa_flags = SA_RESTART | SA_NOCLDSTOP;  // Restart system calls if interrupted
    sigaction(SIGCHLD, &cldsig, NULL);    // Install the handler

    // CREATE LISTENING SOCKET
    // PF_INET = IPv4 protocol family, SOCK_STREAM = TCP socket type
    listen_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0)
    {
        perror("Server: Error creating socket");
        exit(EXIT_FAILURE);
    }

    // SET UP SERVER ADDRESS STRUCTURE
    // Configure the server to listen on all interfaces on the specified port
    memset(&server, 0, sizeof(server));      // Clear the structure
    server.sin_family = AF_INET;             // IPv4 address family
    server.sin_addr.s_addr = htonl(INADDR_ANY);  // Listen on all network interfaces
    server.sin_port = htons(SERVER_PORT);    // Convert port to network byte order

    // BIND SOCKET TO SERVER ADDRESS
    // Associate the socket with our server address and port
    if (bind(listen_sock, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("Server: Error binding socket");
        exit(EXIT_FAILURE);
    }

    // START LISTENING FOR INCOMING CONNECTIONS
    // 5 = backlog queue size for pending connections
    if (listen(listen_sock, 5) < 0)
    {
        perror("Server: Error listening");
        exit(EXIT_FAILURE);
    }

    printf("Poem Server: Ready to serve poems on port %d\n", SERVER_PORT);

    // MAIN SERVER LOOP - RUNS FOREVER
    // Continuously accepts new client connections
    while (1)
    {
        client_len = sizeof(client);
        
        // ACCEPT NEW CLIENT CONNECTION
        // accept() blocks until a client connects, then returns a new socket for that client
        client_sock = accept(listen_sock, (struct sockaddr *)&client, &client_len);
        if (client_sock < 0)
        {
            perror("Server: Error accepting connection");
            continue;  // Continue to next iteration if accept fails
        }

        // GET CLIENT INFORMATION FOR LOGGING
        // Resolve client IP address to hostname for better logs
        client_details = gethostbyaddr((void *)&client.sin_addr.s_addr, 4, AF_INET);
        if (client_details == NULL)
        {
            perror("Server: Error resolving client address");
            close(client_sock);
            continue;
        }

        printf("Server: Accepted connection from %s on port %d\n",
               client_details->h_name, ntohs(client.sin_port));

        // FORK A CHILD PROCESS TO HANDLE THE CLIENT
        // This allows server to handle multiple clients concurrently
        if ((pid = fork()) == 0)
        {
            // CHILD PROCESS - HANDLES CLIENT COMMUNICATION
            close(listen_sock);          // Child does not need listening socket
            manage_connection(client_sock, client_sock);  // Process client request
            exit(EXIT_SUCCESS);          // Exit when client disconnects
        }
        else
        {
            // PARENT PROCESS - CONTINUES LISTENING FOR NEW CONNECTIONS
            close(client_sock);          // Parent does not need client socket
            printf("Server: Process %d will service this client\n", pid);
        }
    }

    close(listen_sock);
    return 0;
}

/*
 * CLIENT CONNECTION HANDLER
 * in: socket descriptor for reading from client
 * out: socket descriptor for writing to client
 *
 * This function:
 * - Generates a random number of poem lines
 * - Builds proper HTTP response with headers
 * - Sends the poem to the client
 * - Closes the connection
 */
void manage_connection(int in, int out)
{
    char buffer[BUFFER_SIZE];      // Buffer for reading (though we do not read client data)
    char response[BUFFER_SIZE * 2]; // Buffer for building HTTP response
    char prefix[100];              // Prefix for logging messages
    int rc;                        // Read count (unused in this version)
    char hostname[40];             // Server hostname for logging

    // PREPARE SERVER IDENTIFICATION FOR LOGGING
    gethostname(hostname, sizeof(hostname));
    snprintf(prefix, sizeof(prefix), "PoemServer[%d]: ", getpid());

    printf("%sServing poem to client\n", prefix);

    // GENERATE RANDOM NUMBER OF LINES TO SERVE
    // rand() % total_lines gives 0 to (total_lines-1), so +1 makes it 1 to total_lines
    int num_lines = (rand() % total_lines) + 1;
    
    // BUILD THE HTTP RESPONSE WITH POEM CONTENT
    memset(response, 0, sizeof(response));  // Clear response buffer
    
    // ADD HTTP HEADERS
    // Simple HTTP response indicating success
    strcat(response, "HTTP/1.1 200 OK\r\n");
    // Content-Type header specifies we are sending plain text
    strcat(response, "Content-Type: text/plain\r\n");
    
    // CALCULATE CONTENT LENGTH
    // Must calculate exact length for Content-Length header
    int content_length = 0;
    for (int i = 0; i < num_lines; i++) {
        // Add length of each line plus newline character
        content_length += strlen(poem_lines[i]) + 1;
    }
    
    // ADD CONTENT-LENGTH HEADER
    char length_header[100];
    snprintf(length_header, sizeof(length_header), "Content-Length: %d\r\n", content_length);
    strcat(response, length_header);
    
    // ADD CONNECTION HEADER AND END OF HEADERS
    strcat(response, "Connection: close\r\n");  // Close connection after response
    strcat(response, "\r\n");  // Blank line separates headers from body

    // ADD POEM LINES TO RESPONSE BODY
    // Copy each selected line followed by a newline character
    for (int i = 0; i < num_lines; i++) {
        strcat(response, poem_lines[i]);
        strcat(response, "\n");
    }

    // SEND COMPLETE RESPONSE TO CLIENT
    // write() sends the entire HTTP response including headers and poem body
    write(out, response, strlen(response));
    
    // LOG THE TRANSACTION
    printf("%sSent %d lines of poem (%d bytes) to client\n", prefix, num_lines, content_length);
    
    // CLOSE CONNECTION AND EXIT CHILD PROCESS
    close(in);
    exit(EXIT_SUCCESS);
}

/*
 * SIGNAL HANDLER FOR CHILD PROCESS CLEANUP
 * signo: signal number (should be SIGCHLD)
 *
 * Prevents zombie processes by:
 * - Non-blocking wait for terminated children
 * - Handling multiple SIGCHLD signals in quick succession
 */
void handle_sigchld(int signo)
{
    /*
     * waitpid() with WNOHANG returns immediately if no child has terminated
     * -1 means wait for any child process
     * NULL means we don't care about exit status
     * Loop continues until all terminated children are reaped
     */
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
