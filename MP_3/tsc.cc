#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

// Coord info
std::string c_hostname;
std::string c_port;

// Meta Client Info
int c_id;

Message MakeMessage(const int &username, const std::string &msg)
{
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp *timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}

class Client : public IClient
{
public:
    Client(const std::string &hname,
           const int &uname,
           const std::string &p)
        : hostname(hname), username(uname), port(p)
    {
    }

protected:
    virtual int connectTo();
    virtual IReply processCommand(std::string &input);
    virtual void processTimeline();

private:
    std::string hostname;
    int username;
    std::string port;
    // You can have an instance of the client stub
    // as a member variable.
    std::unique_ptr<SNSService::Stub> stub_;

    IReply Login();
    IReply List();
    IReply Follow(const int &username2);
    void Timeline(const int &username);
};

void printUsage(std::string arg = "")
{
    if (arg != "")
    {
        std::cerr << "Bad argument: " << arg << std::endl;
    }
    std::cerr << "Usage:" << std::endl;
    std::cerr << "$./client -cip <coordinatorIP> -cp <coordinatorPort> -id <clientId>" << std::endl;

    exit(1);
}

int main(int argc, char **argv)
{

    if (argc != 7)
    {
        printUsage();
    }

    c_hostname = "localhost";
    c_port = "3010";
    c_id = -1;

    for (int i = 1; i < argc; i++)
    {
        std::string arg(argv[i]);

        if (argc == i + 1)
        {
            printUsage(arg);
        }

        if (arg == "-cip")
        {
            c_hostname = argv[i + 1];
            i++;
        }
        else if (arg == "-cp")
        {
            c_port = argv[i + 1];
            if (c_port.size() > 6)
                printUsage(arg);
            i++;
        }
        else if (arg == "-id")
        {
            c_id = atoi(argv[i + 1]);
            if (c_id < 0)
            {
                printUsage(arg);
            }
            i++;
        }
    }

    //std::string username = "u" + std::to_string(c_id);

    Client myc(c_hostname, c_id, c_port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
    std::string login_info = hostname + ":" + port;
    stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
        grpc::CreateChannel(
            login_info, grpc::InsecureChannelCredentials())));

    IReply ire = Login();
    if (!ire.grpc_status.ok())
    {
        return -1;
    }
    return 1;
}

IReply Client::processCommand(std::string &input)
{
    IReply ire;
    std::size_t index = input.find_first_of(" ");
    if (index != std::string::npos)
    {
        std::string cmd = input.substr(0, index);

        /*
        if (input.length() == index + 1) {
            std::cout << "Invalid Input -- No Arguments Given\n";
        }
        */

        int argument = atoi(input.substr(index + 1, (input.length() - index)).c_str());

        if (cmd == "FOLLOW")
        {
            return Follow(argument);
        }
    }
    else
    {
        if (input == "LIST")
        {
            return List();
        }
        else if (input == "TIMELINE")
        {
            ire.comm_status = SUCCESS;
            return ire;
        }
    }

    ire.comm_status = FAILURE_INVALID;
    return ire;
}

void Client::processTimeline()
{
    Timeline(username);
}

IReply Client::List()
{
    // Data being sent to the server
    Request request;
    request.set_username(username);

    // Container for the data from the server
    ListReply list_reply;

    // Context for the client
    ClientContext context;

    Status status = stub_->List(&context, request, &list_reply);
    IReply ire;
    ire.grpc_status = status;
    // Loop through list_reply.all_users and list_reply.following_users
    // Print out the name of each room
    if (status.ok())
    {
        ire.comm_status = SUCCESS;
        std::string all_users;
        std::string following_users;
        for (int s : list_reply.all_users())
        {
            ire.all_users.push_back(std::to_string(s));
        }
        for (int s : list_reply.followers())
        {
            ire.followers.push_back(std::to_string(s));
        }
    }
    return ire;
}

IReply Client::Follow(const int &username2)
{
    Request request;
    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;
    ClientContext context;

    Status status = stub_->Follow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "unkown user name")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "unknown follower username")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "you have already joined")
    {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    }
    else if (reply.msg() == "Follow Successful")
    {
        ire.comm_status = SUCCESS;
    }
    else
    {
        ire.comm_status = FAILURE_UNKNOWN;
    }
    return ire;
}

IReply Client::Login()
{
    Request request;
    request.set_username(username);
    Reply reply;
    ClientContext context;

    Status status = stub_->Login(&context, request, &reply);

    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "you have already joined")
    {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    }
    else
    {
        ire.comm_status = SUCCESS;
    }
    return ire;
}

void Client::Timeline(const int &username)
{
    ClientContext context;

    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
        stub_->Timeline(&context));

    // Thread used to read chat messages and send them to the server
    std::thread writer([username, stream]()
                       {
            std::string input = "Set Stream";
            Message m = MakeMessage(username, input);
            stream->Write(m);
            while (1) {
            input = getPostMessage();
            m = MakeMessage(username, input);
            stream->Write(m);
            }
            stream->WritesDone(); });

    std::thread reader([username, stream]()
                       {
            Message m;
            while(stream->Read(&m)){

            google::protobuf::Timestamp temptime = m.timestamp();
            std::time_t time = temptime.seconds();
            displayPostMessage(std::to_string(m.username()), m.msg(), time);
            } });

    // Wait for the threads to finish
    writer.join();
    reader.join();
}
