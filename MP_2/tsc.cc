#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <iostream>
#include <string>
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
	
	ss >> token;
	switch (token[0]) {
	    case 'F':
	        request.set_username(username);
	        ss >> uname;
	        if (ss.fail()) {
	            ire.comm_status = FAILURE_INVALID_USERNAME;
	            break;
	        }
	        request.add_arguments(uname);
	        status = stub_->Follow(&context, request, &reply);
	        if (status.ok()) {
                ire.comm_status = SUCCESS;
            } else {
                ire.comm_status = FAILURE_NOT_EXISTS;
            }
	        break;
	    case 'U':
	        request.set_username(username);
	        ss >> uname;
	        if (ss.fail()) {
	            ire.comm_status = FAILURE_INVALID_USERNAME;
	            break;
	        }
	        request.add_arguments(uname);
	        status = stub_->UnFollow(&context, request, &reply);
	        if (status.ok()) {
                ire.comm_status = SUCCESS;
            } else {
                ire.comm_status = FAILURE_NOT_EXISTS;
            }
	        break;
	    case 'L':
	        request.set_username(username);
	        status = stub_->List(&context, request, &reply);
	        if (status.ok()) {
                ire.comm_status = SUCCESS;
            } else {
                ire.comm_status = FAILURE_UNKNOWN;
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
        //m.timestamp() = TimeUtil::TimeTToTimestamp(std::time(nullptr));
        stream->Write(m);
    }
}































