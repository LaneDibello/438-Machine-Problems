#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <iostream>
#include <fstream>
#include <string>
#include <queue>
#include <stack>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include <google/protobuf/util/time_util.h>
#include "client.h"

#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :hostname(hname), username(uname), port(p)
            {}
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        // You can have an instance of the client stub
        // as a member variable.
        std::unique_ptr<SNSService::Stub> stub_;
};

enum IStatus msgCodeToStatus(std::string msg){
    switch(msg[0]) {
        case '0':
            return SUCCESS;
        case '1': //Input username already exists
            return FAILURE_ALREADY_EXISTS;
        case '2': //Input username does not exists
            return FAILURE_NOT_EXISTS;
        case '3': //Command failed with invalid username
            return FAILURE_INVALID_USERNAME;
        case '4': //Command failed with invalid command
            return FAILURE_INVALID;
        case '5': //Command failed with unknown reaso
        default:
            return FAILURE_UNKNOWN;
    }
}

void read_timeline(std::shared_ptr<ClientReaderWriter<Message, Message>> stream);

void write_timeline(std::shared_ptr<ClientReaderWriter<Message, Message>> stream, std::string username);

int main(int argc, char** argv) {

    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
        switch(opt) {
            case 'h':
                hostname = optarg;break;
            case 'u':
                username = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
	std::shared_ptr<Channel> channel = CreateChannel(hostname + ":" + port, grpc::InsecureChannelCredentials());
	stub_ = SNSService::NewStub(channel);
	
	ClientContext context;
	Status status;
	Request request;
	Reply reply;
	request.set_username(username);
	status = stub_->Login(&context, request, &reply);
	if (!status.ok() /*|| reply.msg() == "Somethien for already exists"*/){
	    return -1;
	}
	
    return 1; // return 1 if success, otherwise return -1
}

IReply Client::processCommand(std::string& input)
{
	std::istringstream ss(input);
	std::string token;
	std::string uname;
	ClientContext context;
	Status status;
	Request request;
	Reply reply;
	IReply ire;
	std::string r_msg;
	
	ss >> token;
	switch (token[0]) {
	    case 'F':
	        request.set_username(username);
	        ss >> uname;
	        if (ss.fail()) {
	            ire.comm_status = FAILURE_INVALID;
	            break;
	        }
	        request.add_arguments(uname);
	        status = stub_->Follow(&context, request, &reply);
	        r_msg = reply.msg();
	        if (r_msg == "0") {
                ire.comm_status = SUCCESS;
            } else {
                ire.comm_status = msgCodeToStatus(r_msg);
            }
	        break;
	    case 'U':
	        request.set_username(username);
	        ss >> uname;
	        if (ss.fail()) {
	            ire.comm_status = FAILURE_INVALID;
	            break;
	        }
	        request.add_arguments(uname);
	        status = stub_->UnFollow(&context, request, &reply);
	        r_msg = reply.msg();
	        if (r_msg == "0") {
                ire.comm_status = SUCCESS;
            } else {
                ire.comm_status = msgCodeToStatus(r_msg);
            }
	        break;
	    case 'L':
	        request.set_username(username);
	        status = stub_->List(&context, request, &reply);
	        r_msg = reply.msg();
	        if (r_msg == "0") {
                ire.comm_status = SUCCESS;
            } else {
                ire.comm_status = msgCodeToStatus(reply.msg());
            }
            for (int i = 0; i < reply.all_users_size(); i++){
                ire.all_users.push_back(reply.all_users(i));
            }
            for (int i = 0; i < reply.following_users_size(); i++){
                ire.following_users.push_back(reply.following_users(i));
            }
	        break;
	    case 'T':
            //Do nothing?
            ire.comm_status = SUCCESS;
	        break;
	    default:
	        perror(("Bad token: " + token).c_str());
	        ire.comm_status = FAILURE_INVALID;
	        break;
	}
	
	//Do something with 'reply.msg()'?
    
	ire.grpc_status = status;
    return ire;
}

void Client::processTimeline()
{
	ClientContext context;
	
    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(stub_->Timeline(&context));
    
    std::ifstream tin("Timelines/" + username + ".tml");
    std::stack<std::string> msg_q;
    while (!tin.eof()){
      std::string pline;
      getline(tin, pline);
      if (pline.size() < 2) break;
      msg_q.push(pline);
    }
    for (int i = 0; !msg_q.empty() && i < 20; i++) {
        std::cout << msg_q.top() << std::endl;
        msg_q.pop();
    }
      
    
    //Send join msg
    Message m;
    m.set_username(username);
    m.set_msg("\x02"); 
    stream->Write(m);
    
    std::thread read_t(read_timeline, stream);
    std::thread write_t(write_timeline, stream, username);
    read_t.join();
    write_t.join();
}

void read_timeline(std::shared_ptr<ClientReaderWriter<Message, Message>> stream) {
    for(;;){
        Message m;
        while (!stream->Read(&m)) {}
        time_t t = TimeUtil::TimestampToTimeT(m.timestamp());
        displayPostMessage(m.username(), m.msg(), t);
    }
}

void write_timeline(std::shared_ptr<ClientReaderWriter<Message, Message>> stream, std::string username) {
    for(;;){
        Message m;
        m.set_username(username);
        m.set_msg(getPostMessage());
        std::time_t t = std::time(nullptr);
        Timestamp* ts = m.mutable_timestamp();
        *ts = TimeUtil::TimeTToTimestamp(t);
        stream->Write(m);
    }
}































