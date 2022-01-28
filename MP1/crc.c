#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "interface.h"

int connect_to(const char *host, const int port);
struct Reply process_command(const int sockfd, char* command);
void process_chatmode(const char* host, const int port);

//threads
void recv_thread(void* sockfd);
void send_thread(void* sockfd);

int main(int argc, char** argv) 
{
	if (argc != 3) {
		fprintf(stderr,
				"usage: enter host address and port number\n");
		exit(1);
	}

    display_title();
    
	while (1) {
	
		int sockfd = connect_to(argv[1], atoi(argv[2]));
    
		char command[MAX_DATA];
        get_command(command, MAX_DATA);

		struct Reply reply = process_command(sockfd, command);
		display_reply(command, reply);
		
		touppercase(command, strlen(command) - 1);
		if (strncmp(command, "JOIN", 4) == 0) {
			printf("Now you are in the chatmode\n");
			process_chatmode(argv[1], reply.port);
		}
	
		close(sockfd);
    }

    return 0;
}

/*
 * Connect to the server using given host and port information
 *
 * @parameter host    host address given by command line argument
 * @parameter port    port given by command line argument
 * 
 * @return socket fildescriptor
 */
int connect_to(const char *host, const int port)
{
	int rv;
    struct addrinfo hints, *res;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    char port_no[20];
    itoa(port, port_no, 10);
    
    if ((rv = getaddrinfo(host, port_no, &hints, &res)) != 0) { 
    	perror("Failed to obtain adress info");
    	exit(1);
    }
    
    if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
    	perror("Failed ot open socket");
    	exit(1);
    }
    
    if (connect(sockfd, res->ai_addr, res->ai_addrlen)<0) {
    	perror("Failed to connect socket");
    	exit(1);
    }

	return sockfd;
}

/* 
 * Send an input command to the server and return the result
 *
 * @parameter sockfd   socket file descriptor to commnunicate
 *                     with the server
 * @parameter command  command will be sent to the server
 *
 * @return    Reply    
 */
struct Reply process_command(const int sockfd, char* command)
{
	//Send Command
	int count;
    if((count = send(sockfd, command, strlen(command) + 1, 0)) < 0){ 
    	perror("Send failed in process_command");
    }
    
    //Recieve Reply
    struct Reply reply;
    if ((count = recv(sockfd, &reply, sizeof(struct Reply), 0)) == -1){
    	perror("Recieve failed in process_command");
    }

	return reply;
}

/* 
 * Get into the chat mode
 * 
 * @parameter host     host address
 * @parameter port     port
 */
void process_chatmode(const char* host, const int port)
{
	int crsock = connect_to(host, port);
	
	//make threads
	pthread_t rec;
	pthread_t sen;
	pthread_create(&rec, NULL, recv_thread, (void*)crsock);
	pthread_create(&sen, NULL, send_thread, (void*)crsock);
	
	//wait for program to end
	pthread_join(rec, NULL);
	pthread_join(sen, NULL);
    
}

void recv_thread(void* sockfd){
	while(1){
		//Recieve Chats
	    char buffer[MAX_DATA];
	    if ((count = recv(sockfd, buffer, MAX_DATA, 0)) == -1){
	    	perror("Recieve failed in recv_thread");
	    }
	    display_message(buffer);
	}
}

void send_thread(void* sockfd){
	while(1){
		//Send Chat
		char buffer[MAX_DATA];
		get_message(buffer, MAX_DATA);
		int count;
	    if((count = send(sockfd, command, strlen(command) + 1, 0)) < 0){ 
	    	perror("Send failed in send_thread");
	    }
	}
}