#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "interface.h"

short lastport = 1025;

struct servEntry {
    char* name;
    int port;
    pthread_t tid;
}

void chat_server(void* port){
    //Create a server socket and do all the bindings etc
    
    //Enter operation loop
        //block until a message is recieved
        //broadcast the message out to every server
}


int main(int argc, char** argv){
    
    if(argc < 2) {
        perror("Please supply a port number!");
        exit(1);
    }
    
    char* port_no = strdup(argv[1]);
    
    //Create a server socket and do all the bindings etc
    int rv;
    struct addrinfo hints, *serv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port_no, &hints, &serv)) != 0) {
        perror("getaddrinfo for server failed");
        exit(1);
    }
    if((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
        perror("Server Socket Creation failed");
        exit(1);
    }
    if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1) {
        perror("Server Bind Failed"); 
        close(sockfd);
        exit(1);
    }
    freeaddrinfo(serv);
    if (listen(sockfd, 20) == -1) {
        perror("Listener for server Failed");
        close(sockfd);
        exit(1);
    }
    
    //Enter operation loop
    while(1){
        //block until it recieves a command
        char commandbuf[MAX_DATA];
        int count;
        if ((count = recv(sockfd, commandbuf, MAX_DATA, 0)) == -1) {
            perror("Recieve Failed for server");
            return errno;
        }
        switch(commandbuf[0]){
        //CREATE
            case 'C':
            //select a new port
            if(lastport==atoi(port_no)) lastport++;
            
            //generate a chat_server thread with that port
            pthread_t cst;
            char* lport;
            itoa(lastport, lport, 10);
            pthread_create(&cst, NULL, chat_server, (void*)lport);
            lastport++;
            
            //add port, name, and thread_id to database
            
            //supply the port to the client
            
            break;
        //DELETE
            case 'D':
            //find entry in database using the name
            //signal chat_server thread to shut down
            //remove database entry
            //reply to client
            break;
        //JOIN
            case 'J':
            //find entry in database
            //supply the port and memeber count to the user
            //increment member count
            break;
        //LIST
            case 'L':
            //enumerate all the rooms in the database
            //return it to the client
            break;
        }
    }
    
    return 0;
}