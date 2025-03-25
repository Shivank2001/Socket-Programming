#include <stdio.h> 
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <sys/wait.h>
#include <unistd.h> // read(), write(), close()
#define MAX 80 
#define SA struct sockaddr 

// Function for echo 
void func(int connfd) 
{ 
    char buff[MAX]; 
    int n; 
    // loop
    for (;;) { 
        bzero(buff, MAX); 

        // Read message from client and copy it in the buffer 
        int bytes_read = read(connfd, buff, sizeof(buff)); 
        
        // no input is disconnected
        if (bytes_read <= 0) {
            printf("Client disconnected...\n");
            break;
        }
        
        // Print the received message
        printf("From client: %s", buff); 
        
        // Echos received message to client
        printf("echos to client: %s", buff);
        
        // Echo the received message to client 
        write(connfd, buff, bytes_read); 
    
    } 
} 

int main(int argc, char *argv[]) {
    int sock, connfd;
    socklen_t len;
    struct sockaddr_in servaddr, client;
    char port_num[100];
    int port;

    if (argc != 2) {
        printf("Usage: %s <Port>\n", argv[0]);
        return 1;
    }
    // Convert the port argument from string to integer
    port = atoi(argv[1]);

    // Socket creation and verification 
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        printf("Socket creation failed...\n");
        exit(0);
    } else {
        printf("Socket successfully created..\n");
    }
    bzero(&servaddr, sizeof(servaddr));

    // Assign IP (INADDR_ANY) and PORT 
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);  // Bind to all available interfaces
    servaddr.sin_port = htons(port);  // Use the port from the command-line argument

    // Binding new socket to IP address and verification 
    if ((bind(sock, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("Socket bind failed...\n");
        exit(0);
    } 
    else {
        printf("Socket successfully binded on port %d..\n", port);
    }

    // Now server is ready to listen and verification 
    if ((listen(sock, 5)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    } 
    else {
        printf("Server listening on port %d..\n", port);
    }
    len = sizeof(client);

    while (1) {
        // Accept the data packet from client and verification 
        connfd = accept(sock, (SA*)&client, &len);
        if (connfd < 0) {
            printf("Server accept failed...\n");
            exit(0);
        } 
        else {
            printf("Server accepted the client...\n");
        }

        // Fork a child process to handle the client
        pid_t pid = fork();
        if (pid < 0) {
            printf("Fork failed...\n");
            close(connfd);
            continue;
        } 
        else if (pid == 0) { // Child process
            printf("Child PID : %d\n", getpid());
            close(sock); // Close the listening socket in the child process
            func(connfd);  // Handle the client in the child process
            exit(0);       // Exit the child process when done
        } 
        else { // Parent process
            printf("Process id of parent is:%d\n",getppid());
            close(connfd); // Close the connected socket in the parent process
        }

        // Wait for child processes to terminate to prevent zombie processes
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    // After the loop (which normally never happens), close the server socket 
    close(sock);

    return 0;
}