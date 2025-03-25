#include<stdio.h>
#include<stdint.h> 
#include<stdlib.h> //for exit, atoi functions
#include<arpa/inet.h>//used by inet_addr to represent ip address in byte order
#include<string.h>
#include<strings.h>
#include<sys/types.h>  //used to get no of bytes using ssize_t
#include<sys/socket.h> //to create a socket
#include<unistd.h>     //to close the connection
#include<errno.h>      //to check the error number
#include<netinet/in.h> //contains the structure for the socket address details
#define BUFFER_SIZE 20

ssize_t bytesread; 
ssize_t byteswritten;

void error_sys(const char *message);
ssize_t writen(int socket1);

char bufferi[BUFFER_SIZE];  //input buffer
char buffero[BUFFER_SIZE];  //output buffer
 
int main(int argc, char* argv[]){   
    int socket1;
    struct sockaddr_in serveraddr;
    socket1= socket(AF_INET,SOCK_STREAM,0);//AF_INET implies IPV4 protocol, SOCK_STREAM implies TCP connection, 0 implies Internet Protocol

    if(socket1<0){                     // less than 0 implies error
        error_sys("Socket creation failed");
    }
    else{
        printf("Socket created\n");
    }

    if(argc<3){              // no of command line arguments are 3 for client(reduced using the makefile commands but technically same)
        printf("Enter a valid address");
        return 0;
    }
    
    serveraddr.sin_family=AF_INET;
    serveraddr.sin_port=htons(atoi(argv[2]));//converts the port number input in string to integer format
    serveraddr.sin_addr.s_addr=inet_addr(argv[1]);//converts the ip order to byte order network format

    if(connect(socket1,(struct sockaddr*)&serveraddr,sizeof(serveraddr))<0){ //less than 0 implies connection failed
        error_sys("Connection failed");
    }
    printf("Connected to server\n");
    writen(socket1); // call the writen function which takes the socket created as the argument
}
ssize_t writen(int socket1){
    for(;;){
        transmit:
            bzero(bufferi, sizeof(bufferi));// set elements of bufferi to zero before transmission
            if(fgets(bufferi,sizeof(bufferi),stdin)!=NULL){
                if(bufferi[0]=='\n'){       // exit if enter key is pressed without any text
                error_sys("Exited");
                if((errno==EINTR)&&(byteswritten==-1)){ //check for EINTR(slow read) error and transmit again
                    goto transmit;
                }
            }
            byteswritten=write(socket1,bufferi,(strlen(bufferi))); // write to server and store no of bytes
            ssize_t len = strlen(bufferi);
            if (len > 0 && bufferi[len - 1] == '\n') {   // remove newline character 
            bufferi[len - 1] = '\0';
            } 
            ssize_t bytessent=strlen(bufferi);
            printf("Bytes written:%zd\n",bytessent);//display no of bytes sent in the message without newline character
            readagain:
            bzero(buffero, sizeof(buffero));       //initialize output buffer to 0
            bytesread= read(socket1,buffero,sizeof(buffero)); //read from server and store in output buffer
            if(bytesread==0){
                error_sys("End of file"); //if no bytes are read, EOF encountered
            }
            if((errno==EINTR) &&(bytesread==-1)){               //check for EINTR in read data and read again
                goto readagain;
            }
            printf("echo from server:");
            fputs(buffero,stdout);    
            ssize_t lenr = strlen(buffero);
            if (lenr > 0 && buffero[lenr - 1] == '\n') {     // function to remove newline byte and display only char bytes
            buffero[lenr - 1] = '\0';
            } 
            ssize_t bytesrecvd=strlen(buffero);
            printf("Bytes read=%zd\n",bytesrecvd);
            }
            else{
                error_sys("Error reading input");          //for any other error encountered
            }      
    }
}
void error_sys(const char *message){           // function to display specific error message and exit with -1
    perror(message);
    exit(-1);
}


