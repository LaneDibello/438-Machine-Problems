#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using namespace csce438;

struct Client
{
    int username;
    bool connected = true;
    int following_file_size = 0;
    std::vector<Client *> client_followers;
    std::vector<Client *> client_following;
    ServerReaderWriter<Message, Message> *stream = 0;
    bool operator==(const Client &c1) const
    {
        return (username == c1.username);
    }
};

// Coord info
std::string c_hostname;
std::string c_port;

// Meta Server info
int s_id;
bool isMaster;

//Meta Slave info
std::string slave_port;
std::string slave_addr;//likly uneeded for this implementation

// Vector that stores every client that has been created
std::vector<Client> client_db;

// Helper function used to find a Client object given its username
int find_user(int username)
{
    int index = 0;
    for (Client c : client_db)
    {
        if (c.username == username)
            return index;
        index++;
    }
    return -1;
}

static void populate_following(int username, int userindex){
    std::ifstream fol_f(std::to_string(username) + "follows.txt");
    if(fol_f.good()){
        std::string u_id="";
        while(!fol_f.eof()) {
            std::getline(fol_f, u_id, ',');
            if (fol_f.fail()) break;
            client_db[userindex].client_following.push_back(&client_db[find_user(atoi(u_id.c_str()))]);
        }
        fol_f.close();
    }
    std::ifstream flb_f(std::to_string(username) + "followedBy.txt");
    if(flb_f.good()){
        std::string u_id="";
        while(!flb_f.eof()) {
            std::getline(flb_f, u_id, ',');
            if (flb_f.fail()) break;
            client_db[userindex].client_followers.push_back(&client_db[find_user(atoi(u_id.c_str()))]);
        }
        flb_f.close();
    }
}

//Helper login function to allow static calls
static void login_help(const Request *request, Reply *reply){
    Client c;
    int username = request->username();
    int user_index = find_user(username);
    if (user_index < 0)
    {
        c.username = username;
        client_db.push_back(c);
        reply->set_msg("Login Successful!");
        populate_following(username, user_index);
    }
    else
    {
        Client *user = &client_db[user_index];
        if (user->connected)
            reply->set_msg("Invalid Username");
        else
        {
            std::string msg = "Welcome Back " + std::to_string(user->username);
            reply->set_msg(msg);
            user->connected = true;
        }
    }
}

class SNSServiceImpl final : public SNSService::Service
{

    Status List(ServerContext *context, const Request *request, ListReply *list_reply) override
    {
        Client user = client_db[find_user(request->username())];
        int index = 0;
        for (Client c : client_db)
        {
            list_reply->add_all_users(c.username);
        }
        std::vector<Client *>::const_iterator it;
        for (it = user.client_followers.begin(); it != user.client_followers.end(); it++)
        {
            list_reply->add_followers((*it)->username);
        }
        return Status::OK;
    }

    Status Follow(ServerContext *context, const Request *request, Reply *reply) override
    {
        int username1 = request->username();
        int username2 = request->arguments(0);
        int join_index = find_user(username2);
        if (join_index < 0 || username1 == username2)
            reply->set_msg("unkown user name");
        else
        {
            Client *user1 = &client_db[find_user(username1)];
            Client *user2 = &client_db[join_index];
            if (std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end())
            {
                reply->set_msg("you have already joined");
                return Status::OK;
            }
            user1->client_following.push_back(user2);
            user2->client_followers.push_back(user1);
            reply->set_msg("Follow Successful");
            std::ofstream fol_s (std::to_string(username1) + "follows.txt", std::ios::app | std::ios::out | std::ios::in);
            fol_s << username2 << ",";
            fol_s.close();
        }
        return Status::OK;
    }

    Status Login(ServerContext *context, const Request *request, Reply *reply) override
    {
        login_help(request, reply);
        return Status::OK;
    }

    Status Timeline(ServerContext *context,
                    ServerReaderWriter<Message, Message> *stream) override
    {
        Message message;
        Client *c;
        while (stream->Read(&message))
        {
            int username = message.username();
            int user_index = find_user(username);
            c = &client_db[user_index];

            // Write the current message to "username.txt"
            std::string filename = std::to_string(username) + ".txt";
            std::ofstream user_file(filename, std::ios::app | std::ios::out | std::ios::in);
            google::protobuf::Timestamp temptime = message.timestamp();
            std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
            std::string fileinput = time + " :: " + std::to_string(message.username()) + ":" + message.msg() + "\n";
            //"Set Stream" is the default message from the client to initialize the stream
            if (message.msg() != "Set Stream")
                user_file << fileinput;
            // If message = "Set Stream", print the first 20 chats from the people you follow
            else
            {
                if (c->stream == 0)
                    c->stream = stream;
                std::string line;
                std::vector<std::string> newest_twenty;
                std::ifstream in(std::to_string(username) + "following.txt");
                int count = 0;
                // Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
                while (getline(in, line))
                {
                    if (c->following_file_size > 20)
                    {
                        if (count < c->following_file_size - 20)
                        {
                            count++;
                            continue;
                        }
                    }
                    newest_twenty.push_back(line);
                }
                Message new_msg;
                // Send the newest messages to the client to be displayed
                for (int i = 0; i < newest_twenty.size(); i++)
                {
                    new_msg.set_msg(newest_twenty[i]);
                    stream->Write(new_msg);
                }
                continue;
            }
            // Send the message to each follower's stream
            std::vector<Client *>::const_iterator it;
            for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
            {
                Client *temp_client = *it;
                if (temp_client->stream != 0 && temp_client->connected)
                    temp_client->stream->Write(message);
                // For each of the current user's followers, put the message in their following.txt file
                std::string temp_username = std::to_string(temp_client->username);
                std::string temp_file = temp_username + "following.txt";
                std::ofstream following_file(temp_file, std::ios::app | std::ios::out | std::ios::in);
                following_file << fileinput;
                temp_client->following_file_size++;
                std::ofstream user_file(temp_username + ".txt", std::ios::app | std::ios::out | std::ios::in);
                user_file << fileinput;
            }
        }
        // If the client disconnected from Chat Mode, set connected to false
        c->connected = false;
        return Status::OK;
    }
};

class SNSSandMInformImpl final : public SNSSandMInform::Service
{
    
    Status PokeMaster(ServerContext *context, const ServerIdent *request, ServerIdent *response) override
    {
        slave_port = request->port();
        slave_addr = request->addr();
        return Status::OK;
    }

    Status FollowUpdate(ServerContext *context, const FollowData *request, Blep *response) override
    {
        int u_index = find_user(request->id());
        std::vector<int> followers;
        auto fllrs = request->followers();
        std::vector<int> following;
        auto fllig = request->following();

        std::copy(fllrs.begin(), fllrs.end(), followers.begin());
        std::copy(fllig.begin(), fllig.end(), following.begin());

        client_db[u_index].client_followers.clear();
        client_db[u_index].client_following.clear();

        for (int s : followers) client_db[u_index].client_followers.push_back(&client_db[find_user(s)]);
        for (int s : following) client_db[u_index].client_following.push_back(&client_db[find_user(s)]);
    
        return Status::OK;
    }

    Status TimelineUpdate(ServerContext *context, const MsgChunk *request, Blep *response) override
    {
        std::vector<std::string> messages;
        auto msgs = request->msgs();

        std::copy(msgs.begin(), msgs.end(), messages.begin());

        std::ofstream ofs(std::to_string(request->id()), std::ios::trunc);

        for(std::string s : messages){
            ofs << s << std::endl;
        }

        return Status::OK;
    }

    Status LoginUpdate(ServerContext *context, const Request *request, Reply *reply) override
    {
        login_help(request, reply);
        return Status::OK;
    };
};

std::unique_ptr<SNSCoord::Stub> getCoordStub()
{
    // Uses coordinate IP info to connect to it and send the necessary RPC messages
}

void RunServer(std::string port_no)
{
    std::string server_address = "0.0.0.0:" + port_no;
    SNSServiceImpl service;

    //TODO: 
    //Make a coordinator Stub
    //Make an SandMInform stub?
    //Try and spawn a cluster
    //If Master
        //Continue with building server etc
    //If Slave
        //PokeMaster teh pass along credential
        //Continue building etc

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
}

void printUsage(std::string arg = "")
{
    if (arg != "")
    {
        std::cerr << "Bad argument: " << arg << std::endl;
    }
    std::cerr << "Usage:" << std::endl;
    std::cerr << "$./server -cip <coordinatorIP> -cp <coordinatorPort> -p <portNum> -id <idNum> -t <master/slave>" << std::endl;

    exit(1);
}

int main(int argc, char **argv)
{

    if (argc != 11)
    {
        printUsage();
    }

    std::string port = "3010";

    c_hostname = "";
    c_port = "";
    s_id = -1;
    isMaster = false;

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
        else if (arg == "-p")
        {
            port = argv[i + 1];
            if (port.size() > 6)
                printUsage(arg);
            i++;
        }
        else if (arg == "-id")
        {
            s_id = atoi(argv[i + 1]);
            if (s_id < 0)
            {
                printUsage(arg);
            }
            i++;
        }
        else if (arg == "-t")
        {
            std::string op(argv[i + 1]);
            if (op == "master")
                isMaster = true;
            else if (op == "slave")
                isMaster = false;
            else
                printUsage(arg);
            i++;
        }
    }

    RunServer(port);

    return 0;
}
