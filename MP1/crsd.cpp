#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <string> 
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <vector>
#include <queue>
#include <sstream>
#include "interface.h"

int retmsg(int sockfd, void* msg, int msglen);
void* getmsg(int sockfd, int msglen);
void* command_server(void* clientfd);
void* chat_server(void* port);
void* cs_daemon(void* csi);
void* bs_daemon(void* cri);

struct cs_info{
    int fd;
    std::queue<std::string>* msg_queue;
    pthread_mutex_t* ms_lock;
    std::vector<int>* fd_pool;
};

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

    void* cri = malloc(sizeof(cs_info));
    //Create msg queue, lock, and rfd pool
    std::queue<std::string>* msq = new std::queue<std::string>;
    pthread_mutex_t* msqlock = new pthread_mutex_t;
    std::vector<int>* rfds = new std::vector<int>;
    ((cs_info*)cri)->msg_queue = msq;
    ((cs_info*)cri)->ms_lock = msqlock;
    ((cs_info*)cri)->fd_pool = rfds;

    //Create Broadcast thread that will send to these fds when a msg is queued
    pthread_t u;
    pthread_create(&u, NULL, bs_daemon, cri);
    pthread_detach(u);

    //accept incoming traffic
    while(1){
        int chatfd = accept(sockfd, (struct sockaddr*)NULL, NULL);
        if (chatfd == -1){
            perror("Bad accept on chat server");
            continue;
        }
        void* csi = malloc(sizeof(cs_info));
        ((cs_info*)csi)->fd = chatfd;
        ((cs_info*)csi)->msg_queue = msq;
        ((cs_info*)csi)->ms_lock = msqlock;
        rfds->push_back(chatfd);
        pthread_t t;
        pthread_create(&t, NULL, cs_daemon, csi);
        pthread_detach(t);
    }

    
}

void enqueue_msg(std::string msg, std::queue<std::string>* mq, pthread_mutex_t* mql){
    pthread_mutex_lock(mql);
    mq->push(msg);
    pthread_mutex_unlock(mql);
}

std::string dequeue_msg(std::queue<std::string>* mq, pthread_mutex_t* mql) {
    pthread_mutex_lock(mql);
    std::string ret = mq->front();
    mq->pop();
    pthread_mutex_unlock(mql);
    return ret;
}

void* cs_daemon(void* csi){
    //Enter operation loop
    while(1){
        //block until a message is recieved
        char msgbuff[MAX_DATA];
        memcpy(msgbuff, getmsg(((cs_info*)csi)->fd, MAX_DATA), MAX_DATA);
        //broadcast the message out to every client
        std::string msg(msgbuff);
        if(msg.length() < 1) continue;
        enqueue_msg(msg, ((cs_info*)csi)->msg_queue, ((cs_info*)csi)->ms_lock);
    }
}

void* bs_daemon(void* cri){
    //Enter operation loop
    while(1){
        while(((cs_info*)cri)->msg_queue->empty()) usleep(20000); //20 ms ping
        std::string msg = dequeue_msg(((cs_info*)cri)->msg_queue, ((cs_info*)cri)->ms_lock);
        if(msg.length() < 1) continue;
        for (int i = 0; i < ((cs_info*)cri)->fd_pool->size(); i++){
            retmsg(((cs_info*)cri)->fd_pool->at(i), (void*)msg.c_str(), msg.length() + 1);
        }
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
        for (int i = 0; i < chatDB.size(); i++){
            if (chatDB.at(i).name == chat_name){
                memb_count = chatDB.at(i).member_count;
                chatDB.at(i).member_count++; //for joining user
                port_no = chatDB.at(i).port;
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
        fprintf(stderr, "\nError on thread ID: %lu, msglen: %i, sockfd: %i, msg: %s", pthread_self(), msglen, sockfd, (char*)msg);
    	perror("Send failed in retmsg");
        pthread_exit(NULL);
    }
    //DEBUG
    //std::cout << "sent msg: " << std::string((char*)msg) << std::endl;
    ///////
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