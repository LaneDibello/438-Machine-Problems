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
void* chat_server(void* port);
void* cs_daemon(void* sockfd);

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
        pthread_exit(NULL);
    }
    if((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
        perror("Chat Server Socket Creation failed");
        pthread_exit(NULL);
    }
    if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1) {
        perror("Chat Server Bind Failed"); 
        close(sockfd);
        pthread_exit(NULL);
    }
    freeaddrinfo(serv);
    if (listen(sockfd, 20) == -1) {
        perror("Listener for Chat server Failed");
        close(sockfd);
        pthread_exit(NULL);
    }

    //accept incoming traffic
    while(1){
        int chatfd = accept(sockfd, (struct sockaddr*)NULL, NULL);
        if (chatfd == -1){
            perror("Bad accept on chat server");
            continue;
        }
        void* fd = malloc(sizeof(chatfd));
        *(int*)fd = chatfd;
        pthread_t t;
        pthread_create(&t, NULL, cs_daemon, fd);
        pthread_detach(t);
    }

    
}

void* cs_daemon(void* sockfd){
    //Enter operation loop
    while(1){
        //block until a message is recieved
        char msgbuff[MAX_DATA];
        memcpy(msgbuff, getmsg(*(int*)sockfd, MAX_DATA), MAX_DATA);
        //broadcast the message out to every client
        retmsg(*(int*)sockfd, msgbuff, MAX_DATA);
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
        char * hlport = new char [lport.length()+1];
        strcpy(hlport, lport.c_str());
        pthread_create(&cst, NULL, chat_server, (void*)hlport);
        pthread_detach(cst);
        
        lastport++;
        se.tid = cst;
        chatDB.push_back(se);
        
        return se.port;
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

pthread_mutex_t DBlock;
DB* chatRooms;

int retmsg(int sockfd, void* msg, int msglen){
    //Send Command
	int count;
    if((count = send(sockfd, (char*)msg, msglen, 0)) < 0){ 
    	perror("Send failed in retmsg");
        printf("\nError on thread ID: %lu, msglen: %i, sockfd: %i, msg: %s", pthread_self(), msglen, sockfd, (char*)msg);
        pthread_exit(NULL);
    }
    return count;
}

void* getmsg(int sockfd, int msglen){
    //Recieve Reply
    int count = 0;
    void* reply = malloc(msglen);
    while (!count){
        if ((count = recv(sockfd, reply, msglen, 0)) == -1){
            fprintf(stderr, "Error on thread ID: %lu, msglen: %i, sockfd: %i", pthread_self(), msglen, sockfd);
    	    perror("\nRecieve failed in getmsg");
            pthread_exit(NULL);
        }
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
        std::stringstream ss;
        ss << "\n-----------\n";
        std::string output;
        switch(commandbuf[0]){
        //CREATE
            case 'C':
            {
                pthread_mutex_lock(&DBlock);
                int nport = chatRooms->add(command.substr(command.find(' ')+1));
                pthread_mutex_unlock(&DBlock);
                if (!nport){
                    //Do room already exist stuff
                    reply.status = FAILURE_ALREADY_EXISTS;
                    break;
                }
                reply.status = SUCCESS;
            }
            break;
        //DELETE
            case 'D':
            {
                pthread_mutex_lock(&DBlock);
                int res = chatRooms->remove(command.substr(command.find(' ')+1));
                pthread_mutex_unlock(&DBlock);
                    if(!res){
                    //Do room not exist stuff
                    reply.status = FAILURE_NOT_EXISTS;
                    break;
                }
                reply.status = SUCCESS;
            }
            break;
        //JOIN
            case 'J':
            {   //find entry in database
                //supply the port and memeber count to the user
                int memb_count;
                int port_no;
                pthread_mutex_lock(&DBlock);
                bool succ = chatRooms->get_info(command.substr(command.find(' ')+1), memb_count, port_no);
                pthread_mutex_unlock(&DBlock);
                if(!succ){
                    reply.status = FAILURE_NOT_EXISTS;
                    break;
                }
                reply.status = SUCCESS;
                reply.num_member = memb_count;
                reply.port = port_no;
            }
            break;
        //LIST
            case 'L':
            {//enumerate all the rooms in the database
                pthread_mutex_lock(&DBlock);
                for (size_t i = 1; i < chatRooms->chatDB.size(); i++)
                {
                    ss << chatRooms->chatDB.at(i).name << std::endl;
                }
                pthread_mutex_unlock(&DBlock);
                reply.status = SUCCESS;
                output = ss.str();
                if (output.length() > 255){
                    output = output.substr(0, 252);
                    output += "...";
                }
                strcpy(reply.list_room, output.c_str());
            }
            break;
        //DEFAULT
            default:
            {
                reply.status = FAILURE_INVALID;
            }
            break;
        }
        retmsg(sockfd, &reply, sizeof(reply));
        if (commandbuf[0] == 'J' && reply.status == SUCCESS) {
            close(sockfd);
            return nullptr;
        }
    }
    
}

int main(int argc, char** argv){
    
    if(argc < 2) {
        perror("Please supply a port number!");
        exit(1);
    }

    chatRooms = new DB(atoi(argv[1]));
    
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
        void* fd = malloc(sizeof(clientfd));
        *(int*)fd = clientfd;
        pthread_t t;
        pthread_create(&t, NULL, command_server, fd);
        pthread_detach(t);
    }

    close(sockfd);
    
    return 0;
}