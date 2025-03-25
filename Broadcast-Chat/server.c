#include <stdio.h>      
#include <stdlib.h>    
#include <string.h>     
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <netinet/ip.h> 
#include <netdb.h>      
#include <sys/select.h> 
#include <sys/types.h>  
#include <unistd.h>     
#include <fcntl.h>      
#include <time.h>       

#define USERNAME_TAKEN "Error: This username is already in use."
#define SERVER_FULL "Error: The server cannot accept more connections."

#define JOIN 	2
#define FWD 	3
#define SEND 	4
#define NAK		5
#define OFFLINE 6
#define ACK		7
#define ONLINE	8
#define IDLE	9

#define IDLE_TIMEOUT_SECONDS 10 

// SBCP attributes 
struct SBCPAttributes {
  int type :16;
  int payloadLength :16;
  char payload[512];
};

// SBCP message structure
struct sbcpmsg {
  int version :9;
  char type :7;
  int length :16;
  struct SBCPAttributes attributes[4];
};

// Details of the clients
struct ClientDetails {
  char username[16];
  int socketDesc;
  time_t lastActive; 
};

//JOIN SBCP message
void createJoinsbcpmsg(struct sbcpmsg **sbcpmsg, char username[16]) {	
	(*sbcpmsg)->version = 3;
	(*sbcpmsg)->length = 1;
	(*sbcpmsg)->type = 2;
	(*sbcpmsg)->attributes[0].type = 2;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(username);
	strcpy((*sbcpmsg)->attributes[0].payload, username);					
}

//NAK message 
void createNaksbcpmsg(struct sbcpmsg **sbcpmsg, char *reason) {
	(*sbcpmsg)->version = 3;					 
	(*sbcpmsg)->type = 5;
	(*sbcpmsg)->length = 1;
    (*sbcpmsg)->attributes[0].type = 1;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(reason);
	strcpy((*sbcpmsg)->attributes[0].payload, reason);	
}

// ACK message sent by a successful JOIN
void createAcksbcpmsg(struct sbcpmsg **sbcpmsg, char *string, char username[16], int clientCount) {
	(*sbcpmsg)->version = 3;					 
	(*sbcpmsg)->type = 7;
	(*sbcpmsg)->length = 3;
    (*sbcpmsg)->attributes[0].type = 2;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(username);
	strcpy((*sbcpmsg)->attributes[0].payload, username);	
	(*sbcpmsg)->attributes[1].type = 3;
	char str[15];
	sprintf(str, "%d", clientCount);		
	(*sbcpmsg)->attributes[1].payloadLength = strlen(str);
	strcpy((*sbcpmsg)->attributes[1].payload, str);	
	(*sbcpmsg)->attributes[2].type = 4;
	(*sbcpmsg)->attributes[2].payloadLength = strlen(string);
	strcpy((*sbcpmsg)->attributes[2].payload, string);	
}

// SEND message 
void createSendsbcpmsg(struct sbcpmsg **sbcpmsg, char username[16], char inputBuffer[512]) {
	(*sbcpmsg)->version = 3;
	(*sbcpmsg)->type = 4;
	(*sbcpmsg)->length = 2;
	(*sbcpmsg)->attributes[0].type = 2;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(username);
	strcpy((*sbcpmsg)->attributes[0].payload, username);
	(*sbcpmsg)->attributes[1].type = 4;
	(*sbcpmsg)->attributes[1].payloadLength = strlen(inputBuffer);
	strcpy((*sbcpmsg)->attributes[1].payload, inputBuffer);	
}

// creates ONLINE message 
void createOnlinesbcpmsg(struct sbcpmsg **sbcpmsg, char message[512]) {
	(*sbcpmsg)->version = 3;
	(*sbcpmsg)->type = 8;
	(*sbcpmsg)->length = 1;
	(*sbcpmsg)->attributes[0].type = 4;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(message);
	strcpy((*sbcpmsg)->attributes[0].payload, message);
}

// FWD message
void createFwdsbcpmsg(struct sbcpmsg **sbcpmsg, char message[512]) {
	(*sbcpmsg)->version = 3;
	(*sbcpmsg)->type = 3;
	(*sbcpmsg)->length = 1;
	(*sbcpmsg)->attributes[0].type = 4;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(message);
	strcpy((*sbcpmsg)->attributes[0].payload, message);
}

//OFFLINE message 
void createOfflinesbcpmsg(struct sbcpmsg **sbcpmsg, char message[512]) {
	(*sbcpmsg)->version = 3;
	(*sbcpmsg)->type = 6;
	(*sbcpmsg)->length = 1;
	(*sbcpmsg)->attributes[0].type = 4;
	(*sbcpmsg)->attributes[0].payloadLength = strlen(message);
	strcpy((*sbcpmsg)->attributes[0].payload, message);
}

// IDLE message 
void createIdlesbcpmsg(struct sbcpmsg **sbcpmsg, char message[512]) {
    (*sbcpmsg)->version = 3;
    (*sbcpmsg)->type = IDLE; 
    (*sbcpmsg)->length = 1;
    (*sbcpmsg)->attributes[0].type = 4;
    (*sbcpmsg)->attributes[0].payloadLength = strlen(message);
    strncpy((*sbcpmsg)->attributes[0].payload, message, sizeof((*sbcpmsg)->attributes[0].payload) - 1);
    (*sbcpmsg)->attributes[0].payload[sizeof((*sbcpmsg)->attributes[0].payload) - 1] = '\0';
}

int isUsernameValid(char username[16], struct ClientDetails clientList[]);
void processIncomingMessage(struct sbcpmsg incomingMessage, struct ClientDetails clientList[], int clientDescriptor, int maxClients);
void processOfflineClient(struct sbcpmsg incomingMessage, struct ClientDetails clientList[], int clientDescriptor);
void processIdleClients(struct ClientDetails clientList[]);

//fd_set to store the socket descriptors of active connections 
fd_set clientDescriptors, allDescriptors;
int currentClientCount;

int main(int argc, char *argv[]) {
    int sockDescriptor, newClientDescriptor, serverPort, maxClients, maxDescriptors, i;
    char *serverIP;
	struct hostent *host;
    struct sockaddr_storage serverSockaddr, clientSockaddr;
    struct addrinfo hints, *serv;
    int lenSockaddrIn = sizeof(clientSockaddr);
    int selectValue;
    currentClientCount = 0;
    struct timeval timeout;  // Used for select() timeout
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    
    // Parse command-line arguments
    serverIP = argv[1];
    serverPort = atoi(argv[2]);
    maxClients = atoi(argv[3]);

    // Get address info for binding
    if (getaddrinfo(serverIP, argv[2], &hints, &serv) != 0) {
        perror("ERROR: getaddrinfo failed");
        return -1;
    }

    // Create server socket
    sockDescriptor = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol);
    if (sockDescriptor == -1) {
        perror("ERROR: Cannot create socket");
        return -1;
    }
    printf("Socket Created\n");

    // Bind socket
    if (bind(sockDescriptor, serv->ai_addr, serv->ai_addrlen) == -1) {
        perror("ERROR: Unable to bind the socket");
        return -1;
    }
    printf("Completed bind\n");

    // Listen on the socket
    if (listen(sockDescriptor, maxClients) == -1) {
        perror("ERROR: Unable to listen");
        return -1;
    }
    printf("Started Listening\n");

    // Initialize client list and fd_set
    struct ClientDetails clientList[maxClients];
    FD_ZERO(&clientDescriptors); 
    FD_ZERO(&allDescriptors);
    FD_SET(sockDescriptor, &allDescriptors);
    maxDescriptors = sockDescriptor;

    // Main loop for handling clients and checking idle clients
    while (1) {
        clientDescriptors = allDescriptors;

        // Set select timeout to 1 second for regular idle checking
        timeout.tv_sec = 10;  
        timeout.tv_usec = 0;

        // Wait for socket activity with select
        selectValue = select(maxDescriptors + 1, &clientDescriptors, NULL, NULL, &timeout);
        if (selectValue == -1) {
            perror("ERROR: Select failed");
            return -1;
        }

        // Check if select timed out (i.e., no activity) to process idle clients
        if (selectValue == 0) {
            // Process idle clients every second
            processIdleClients(clientList);
            continue;
        }

        // Handle client connections or messages
        for (i = 0; i <= maxDescriptors; i++) {
            if (FD_ISSET(i, &clientDescriptors)) {
                // Handle new client connection
                if (i == sockDescriptor) {
                    newClientDescriptor = accept(sockDescriptor, (struct sockaddr *)&clientSockaddr, (socklen_t *)&lenSockaddrIn);
                    if (newClientDescriptor == -1) {
                        perror("ERROR: Unable to connect to the client");
                    } else {
                        FD_SET(newClientDescriptor, &allDescriptors);
                        if (newClientDescriptor > maxDescriptors) {
                            maxDescriptors = newClientDescriptor;
                        }
                        printf("INFO: Established connection with the client.\n");
                    }
                }
                // Handle messages from existing clients
                else {
                    struct sbcpmsg incomingMessage;
                    int numOfBytes = read(i, (struct sbcpmsg *)&incomingMessage, sizeof(incomingMessage));
                    if (numOfBytes > 0) {
                        // Update the client's lastActive time
                        for (int j = 0; j < currentClientCount; j++) {
                            if (clientList[j].socketDesc == i) {
                                clientList[j].lastActive = time(NULL);
                                break;
                            }
                        }

                        // Process the received message
                        processIncomingMessage(incomingMessage, clientList, i, maxClients);
                    } else {
                        // Handle client disconnection
                        processOfflineClient(incomingMessage, clientList, i);
                    }
                }
            }
        }
    }

    // Cleanup
    close(sockDescriptor);
    freeaddrinfo(serv);
}

void processIncomingMessage(struct sbcpmsg incomingMessage, struct ClientDetails clientList[], int clientDescriptor, int maxClients) {
    char *reason;
    struct sbcpmsg *outputMessage;
    char concatString[512] = {0}, onlineString[512] = {0}, messageString[512] = {0};    
    int j;
    
    if (incomingMessage.type == JOIN) {
        if (currentClientCount < maxClients && isUsernameValid(incomingMessage.attributes[0].payload, clientList)) {
            for (j = 0; j < currentClientCount; j++) {
                strcat(concatString, clientList[j].username);
                strcat(concatString, "\t");
            }
            
            clientList[currentClientCount] = (struct ClientDetails){
                .socketDesc = clientDescriptor,
                .lastActive = time(NULL)
            };
            strncpy(clientList[currentClientCount].username, incomingMessage.attributes[0].payload, 16);
            currentClientCount++;

            outputMessage = malloc(sizeof(struct sbcpmsg));
            createAcksbcpmsg(&outputMessage, concatString, incomingMessage.attributes[0].payload, currentClientCount);
			printf("INFO: Sending ACK response for user: %s\n", incomingMessage.attributes[0].payload);
            write(clientDescriptor, outputMessage, sizeof(*outputMessage));
            free(outputMessage);

            snprintf(onlineString, sizeof(onlineString), "%s is Online.", incomingMessage.attributes[0].payload);
            outputMessage = malloc(sizeof(struct sbcpmsg));
            createOnlinesbcpmsg(&outputMessage, onlineString);
            for (j = 0; j < currentClientCount - 1; j++) {
                write(clientList[j].socketDesc, outputMessage, sizeof(*outputMessage));
            }
            free(outputMessage);

            printf("INFO: %s has joined.\n", incomingMessage.attributes[0].payload);
        } else {
            reason = (currentClientCount >= maxClients) ? SERVER_FULL : USERNAME_TAKEN;
            outputMessage = malloc(sizeof(struct sbcpmsg));
            createNaksbcpmsg(&outputMessage, reason);
			printf("INFO: Sending NAK response for user: %s\n", incomingMessage.attributes[0].payload);
            write(clientDescriptor, outputMessage, sizeof(*outputMessage));
            free(outputMessage);
            FD_CLR(clientDescriptor, &allDescriptors);
        }
    } else if (incomingMessage.type == SEND) {
        snprintf(concatString, sizeof(concatString), "%s: %s", 
                 incomingMessage.attributes[0].payload, incomingMessage.attributes[1].payload);
        outputMessage = malloc(sizeof(struct sbcpmsg));
        createFwdsbcpmsg(&outputMessage, concatString);
        for (j = 0; j < currentClientCount; j++) {
            if (clientList[j].socketDesc != clientDescriptor) {
                write(clientList[j].socketDesc, outputMessage, sizeof(*outputMessage));
            }
        }
        free(outputMessage);
        printf("INFO: Sent broadcast for user: %s\n", incomingMessage.attributes[0].payload);
    }
}

int isUsernameValid(char username[16], struct ClientDetails clientList[]) {
    for (int i = 0; i < currentClientCount; i++) {
        if (strncmp(clientList[i].username, username, 16) == 0) {
            return 0;
        }
    }
    return 1;
}

void processOfflineClient(struct sbcpmsg incomingMessage, struct ClientDetails clientList[], int clientDescriptor) {
    struct sbcpmsg *outputMessage;
    char offlineMessage[512];
    int clientIndex = -1;

    for (int i = 0; i < currentClientCount; i++) {
        if (clientList[i].socketDesc == clientDescriptor) {
            clientIndex = i;
            break;
        }
    }

    if (clientIndex != -1) {
        snprintf(offlineMessage, sizeof(offlineMessage), "%s is Offline.\n", clientList[clientIndex].username);
        printf("INFO: %s has disconnected.\n", clientList[clientIndex].username);

        outputMessage = malloc(sizeof(struct sbcpmsg));
        createOfflinesbcpmsg(&outputMessage, offlineMessage);

        for (int j = 0; j < currentClientCount; j++) {
            if (j != clientIndex) {
                write(clientList[j].socketDesc, outputMessage, sizeof(*outputMessage));
            }
        }
        free(outputMessage);

        FD_CLR(clientDescriptor, &allDescriptors);
        memmove(&clientList[clientIndex], &clientList[clientIndex + 1], 
                (currentClientCount - clientIndex - 1) * sizeof(struct ClientDetails));
        currentClientCount--;
    }
}

void processIdleClients(struct ClientDetails clientList[]) {
    struct sbcpmsg *outputMessage;
    char idleMessage[512];
    time_t currentTime = time(NULL);

    for (int i = 0; i < currentClientCount; i++) {
        if (difftime(currentTime, clientList[i].lastActive) >= IDLE_TIMEOUT_SECONDS) {
            snprintf(idleMessage, sizeof(idleMessage), "%s", clientList[i].username);
            outputMessage = malloc(sizeof(struct sbcpmsg));
            createIdlesbcpmsg(&outputMessage, idleMessage);

            for (int j = 0; j < currentClientCount; j++) {
                if (j != i) {
                    write(clientList[j].socketDesc, outputMessage, sizeof(*outputMessage));
                }
            }
            free(outputMessage);
        }
    }
}