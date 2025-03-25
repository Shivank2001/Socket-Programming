#include<stdio.h>
#include<stdint.h> 
#include<stdlib.h> //for exit, atoi functions
#include<arpa/inet.h>//used by inet_addr to represent ip address in byte order
#include<string.h>
#include<strings.h>
#include<sys/types.h>  //used to get no of bytes using ssize_t
#include<sys/socket.h> //to create a socket
#include<sys/select.h> //to implement
#include<unistd.h>     //to close the connection
#include<errno.h>      //to check the error number
#include<netinet/in.h> //contains the structure for the socket address details
#define BUFFER_SIZE 1000
#define JOIN 2
#define FWD 3
#define SEND 4
#define NAK 5
#define OFFLINE 6
#define ACK 7
#define ONLINE 8
#define IDLE 9

fd_set readfd, allfd;   //file descriptors for the select function for input descriptor and all descriptors
int max_descriptors=0;

char username[BUFFER_SIZE];
char msg_send[BUFFER_SIZE];
ssize_t bytesread; 
ssize_t byteswritten;
void error_sys(const char *message);
ssize_t writen(int socket1);

char bufferi[BUFFER_SIZE];  //input buffer
char buffero[BUFFER_SIZE];


struct SBCP_attribute{ //structure to specify the attributes of the message
    int Type:16;
    int Length:16;
    char payload[512];

};

struct SBCP_header{   // structure to hold the formatted message
    int vrsn:9;
    char type:7;
    int length:16;
    struct SBCP_attribute attributes[4];
};

void join(struct SBCP_header *msg, char username[16]){   //function to send a join message to server
    msg->vrsn=3;
    msg->length=1;
    msg->type=2;
    msg->attributes[0].Type=2;
    msg->attributes[0].Length=strlen(username);
    strcpy(msg->attributes[0].payload, username);
    printf("Sending JOIN message for user:%s\n",msg->attributes[0].payload);
}

void send_msg(struct SBCP_header *msg, char username[16],char inputbuff[512]){    //function to send normal messages to server
    msg->vrsn=3;
    msg->type=4;
    msg->length=2;
    msg->attributes[0].Type=2;
    msg->attributes[0].Length=strlen(username);
    strcpy(msg->attributes[0].payload, username);
    msg->attributes[1].Type=4;
    msg->attributes[1].Length=strlen(inputbuff);
    strcpy(msg->attributes[1].payload, inputbuff);
}
void idle(struct SBCP_header *msg, char username[16]){       //function to send idle message to server
    msg->vrsn=3;
    msg->length=2;
    msg->type=9;
    msg->attributes[0].Type=2;
    msg->attributes[0].Length=strlen(username);
    strcpy(msg->attributes[0].payload, username);
    //printf("User %s is idle\n",msg->attributes[0].payload);
}
//Define attributes to JOIN SERVER 

int main(int argc, char* argv[]){   
    int socket1;
    struct sockaddr_storage serveraddr; // Use sockaddr_storage to hold both IPv4 and IPv6
    socklen_t addrlen;

    if (argc < 4) {
        printf("Usage: %s <username> <address> <port>\n", argv[0]);
        return 0;
    }

    // Create socket

    strcpy(username, argv[1]);
    
    // Initialize sockaddr_storage
    memset(&serveraddr, 0, sizeof(serveraddr));

    // Check if the address is IPv4
    struct sockaddr_in *addr_ipv4 = (struct sockaddr_in *)&serveraddr;
    if (inet_pton(AF_INET, argv[2], &addr_ipv4->sin_addr) == 1) {
        // IPv4 address
        socket1 = socket(AF_INET, SOCK_STREAM, 0);
        if (socket1 < 0) {
            error_sys("Socket creation failed");
        } else {
            printf("Socket created\n");
        }
        addr_ipv4->sin_family = AF_INET;
        addr_ipv4->sin_port = htons(atoi(argv[3]));
        addrlen = sizeof(struct sockaddr_in);
    } else {
        // Check if the address is IPv6
        struct sockaddr_in6 *addr_ipv6 = (struct sockaddr_in6 *)&serveraddr;
        if (inet_pton(AF_INET6, argv[2], &addr_ipv6->sin6_addr) == 1) {
            // IPv6 address
            socket1 = socket(AF_INET6, SOCK_STREAM, 0);
        if (socket1 < 0) {
            error_sys("Socket creation failed");
        } else {
            printf("Socket created\n");
        }
            addr_ipv6->sin6_family = AF_INET6;
            addr_ipv6->sin6_port = htons(atoi(argv[3]));
            addrlen = sizeof(struct sockaddr_in6);
        } else {
            fprintf(stderr, "Invalid address format\n");
            close(socket1);
            return 1;
        }
    }

    // Connect to the server
    if (connect(socket1, (struct sockaddr *)&serveraddr, addrlen) < 0) {
        error_sys("Connection failed");
    }
    printf("Connected to server\n");

    FD_ZERO(&readfd);
    FD_ZERO(&allfd);
    FD_SET(socket1, &allfd);
    FD_SET(STDIN_FILENO, &allfd);
    max_descriptors = socket1;

    printf("Initiating JOIN\n");
    struct SBCP_header joinmsg;
    join(&joinmsg, username);
    write(socket1, &joinmsg, sizeof(joinmsg));

    writen(socket1);
}


ssize_t writen(int socket1){
    for(;;){
        struct timeval timeout; //used to set the timeout value in select function
        struct SBCP_header idlemsg;
        timeout.tv_sec=10;  //the timeout value is set to 10s
        timeout.tv_usec=0;  
        int select_number;
        readfd=allfd;
        select_number=select(max_descriptors+1, &readfd,NULL,NULL,&timeout); //used to implement IO/multiplexing
        if(select_number<0){
            error_sys("Error:Select failed");
        }
        if(select_number==0){
            //printf("Idle state\n");
            idle(&idlemsg,username);
            write(socket1,&idlemsg,sizeof(idlemsg));
        }
        if(FD_ISSET(STDIN_FILENO, &readfd)){
            bzero(bufferi, sizeof(bufferi));
            fgets(bufferi,sizeof(bufferi),stdin);
            struct SBCP_header sent_msg;
            send_msg(&sent_msg,username,bufferi);
            write(socket1,&sent_msg,sizeof(sent_msg));
        }
        if(FD_ISSET(socket1, &readfd)){
            readagain:
            bzero(buffero, sizeof(buffero));
            struct SBCP_header recv_msg;
            bytesread= read(socket1,&recv_msg,sizeof(recv_msg)); //read from server and store in output buffer
            if(bytesread==0){
                error_sys("End of file"); //if no bytes are read, EOF encountered
            }
            if((errno==EINTR) &&(bytesread==-1)){               //check for EINTR in read data and read again
                goto readagain;
            }
            if(recv_msg.type==3){                  //received message is fwd from server
                printf("%s\n",recv_msg.attributes[0].payload);
            }
            if(recv_msg.type==5){   //received message is NAK
                printf("NAK for %s\n",recv_msg.attributes[0].payload);
                error_sys("Exiting");
            }
            if(recv_msg.type==6){          //received message is offline message
                printf("%s\n",recv_msg.attributes[0].payload);
            }
            
            if(recv_msg.type==7){     //received message is ACK
                printf("ACK from server is %s\n",recv_msg.attributes[0].payload);
                printf("Client Count: %s\n",recv_msg.attributes[1].payload);
                printf("Other users are: %s\n",recv_msg.attributes[2].payload);
                
            }
            if(recv_msg.type==8){    //received message is online
                printf("%s\n",recv_msg.attributes[0].payload);
            }
            if(recv_msg.type==9){
                printf("%s is idle\n",recv_msg.attributes[0].payload); //receieved message is an idle message
            }
        }

    }
}


void error_sys(const char *message){           // function to display specific error message and exit with -1
    perror(message);
    exit(-1);
}

