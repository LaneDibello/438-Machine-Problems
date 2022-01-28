#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <string> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <vector>
#include <sstream>
#include "interface.h"

int retmsg(int sockfd, void* msg, int msglen);
void* getmsg(int sockfd, int msglen);
void* command_server(void* clientfd);

void *chat_server(void* port){
    //Create a server socket and do all the bindings etc
    int rv;
    int sockfd;
    struct addrinfo hints, *serv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, (char*)port, &hints, &serv)) != 0) {
        perror("getaddrinfo for Chat server failed");
        exit(1);
    }
    if((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
        perror("Chat Server Socket Creation failed");
        exit(1);
    }
    if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1) {
        perror("Chat Server Bind Failed"); 
        close(sockfd);
        exit(1);
    }
    freeaddrinfo(serv);
    if (listen(sockfd, 20) == -1) {
        perror("Listener for Chat server Failed");
        close(sockfd);
        exit(1);
    }

    //Enter operation loop
    while(1){
        //block until a message is recieved
        char msgbuff[MAX_DATA];
        memcpy(msgbuff, getmsg(sockfd, MAX_DATA), MAX_DATA);
        //broadcast the message out to every client
        retmsg(sockfd, msgbuff, MAX_DATA);
    }
}

class DB {
    public:
    struct servEntry {
        std::string name;
        int port;
        pthread_t tid;
        size_t member_count;
    };
    
    short lastport = 1025;
    std::vector<servEntry> chatDB;

    DB(int port){
        struct servEntry se;
        se.name = "base control";
        se.port = port;
        se.tid = -1;
        se.member_count = 0;
        chatDB.push_back(se);
    }

    int add(std::string chat_name){
        if (contains(chat_name)) return 0;
        struct servEntry se;
        se.name = chat_name;
        se.member_count = 0;
        if (contains(lastport)) lastport++; //skip over first port
        se.port = lastport;
        pthread_t cst;
        std::string lport = std::to_string(lastport);
        pthread_create(&cst, NULL, chat_server, (void*)lport.c_str());
        lastport++;
        se.tid = cst;
        chatDB.push_back(se);
        pthread_detach(cst);
    }

    bool contains(int port){
        for (struct servEntry se : chatDB){
            if (se.port == port) return true;
        }
        return false;
    }

    bool contains(std::string chat_name){
        for (struct servEntry se : chatDB){
            if (se.name == chat_name) return true;
        }
        return false;
    }

    bool remove(std::string chat_name) {
        for (size_t i = 0; i < chatDB.size(); i++)
        {
            if (chatDB.at(i).name == chat_name){
                //maybe send a msg to the server being deleted?
                chatDB.erase(chatDB.begin() + i);
                return true;
            }
        }
        return false;
    }

    bool get_info(std::string chat_name, int& memb_count, int& port_no) {
        for (struct servEntry se : chatDB){
            if (se.name == chat_name){
                memb_count = se.member_count;
                se.member_count++; //for joining user
                port_no = se.port;
                return true;
            }
        }
        return false;
    }
};

DB* chatRooms;

int retmsg(int sockfd, void* msg, int msglen){
    //Send Command
	int count;
    if((count = send(sockfd, (char*)msg, msglen, 0)) < 0){ 
    	perror("Send failed in retmsg");
        exit(1);
    }
    return count;
}

void* getmsg(int sockfd, int msglen){
    //Recieve Reply
    int count;
    void* reply = malloc(msglen);
    if ((count = recv(sockfd, reply, msglen, 0)) == -1){
    	perror("Recieve failed in getmsg");
        exit(1);
    }
    return reply;
}

void* command_server(void* clientfd){
    int sockfd = *(int*)clientfd;
    //Enter operation loop
    //MOVE TO SEPARTE FUNCTION
    while(1){
        char commandbuf[MAX_DATA];
        memcpy(commandbuf, getmsg(sockfd, MAX_DATA), MAX_DATA);
        std::string command(commandbuf); 
        struct Reply reply;
        std::stringstream ss("CHAT ROOMS:\n-----------\n");
        std::string output;
        switch(commandbuf[0]){
        //CREATE
            case 'C':
            {int nport = chatRooms->add(command.substr(command.find(' ')+1));
            if (!nport){
                //Do room already exist stuff
                reply.status = FAILURE_ALREADY_EXISTS;
                break;
            }
            reply.status = SUCCESS;}
            break;
        //DELETE
            case 'D':
            {if(!chatRooms->remove(command.substr(command.find(' ')+1))){
                //Do room not exist stuff
                reply.status = FAILURE_NOT_EXISTS;
                break;
            }
            reply.status = SUCCESS;
            break;}
        //JOIN
            case 'J':
             {   //find entry in database
                //supply the port and memeber count to the user
                int memb_count;
                int port_no;
                if(!chatRooms->get_info(command.substr(command.find(' ')+1), memb_count, port_no)){
                    reply.status = FAILURE_NOT_EXISTS;
                    break;
                }
                reply.status = SUCCESS;
                reply.num_member = memb_count;
                reply.port = port_no;
            break;}
        //LIST
            case 'L':
                {//enumerate all the rooms in the database
                for (size_t i = 1; i < chatRooms->chatDB.size(); i++)
                {
                    ss << chatRooms->chatDB.at(i).name << std::endl;
                }
                reply.status = SUCCESS;
                output = ss.str();
                if (output.length() > 255){
                    output = output.substr(0, 252);
                    output += "...";
                }
                strcpy(reply.list_room, output.c_str());
            break;}
        //DEFAULT
            default:
            {reply.status = FAILURE_INVALID;
            break;}
        }
        retmsg(sockfd, &reply, sizeof(reply));
        if (commandbuf[0] == 'J' && reply.status == SUCCESS) {
            return;
        }
    }
    
}

int main(int argc, char** argv){
    
    if(argc < 2) {
        perror("Please supply a port number!");
        exit(1);
    }

    DB* chatRooms = new DB(atoi(argv[1]));
    
    char* port_no = strdup(argv[1]);
    int sockfd;
    
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
    
    //Start Accepting
    while (1){
        int clientfd = accept(sockfd, (struct sockaddr*)NULL, NULL);
        if (clientfd == -1){
            perror("Bad accept on server");
            continue;
        }
        pthread_t t;
        pthread_create(&t, NULL, command_server, (void*)&clientfd);
        pthread_detach(t);
    }

    
    return 0;
}