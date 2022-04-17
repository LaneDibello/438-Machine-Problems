#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <thread>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
    std::vector<int> client_followers;
    std::vector<int> client_following;
    ServerReaderWriter<Message, Message> *stream = 0;
    bool operator==(const Client &c1) const
    {
        return (username == c1.username);
    }
};

// Coord info
std::string c_hostname;
std::string c_port;

//Stubs
std::unique_ptr<csce438::SNSCoord::Stub> c_stub; //Coordinator
std::unique_ptr<csce438::SNSSandMInform::Stub> s_stub = nullptr; //slave/master

// Meta Server info
int s_id;
bool isMaster;
std::string my_port;
std::string my_hostname;

//Meta Slave info
std::string slave_port;
std::string slave_addr;//likly uneeded for this implementation

// Vector that stores every client that has been created
std::vector<Client> client_db;

void heartbeat() {
    Status s;
    for (;;){
        HrtBt hb, rhb;
        hb.set_id(s_id);
        hb.set_master(isMaster);
        ClientContext context;
        std::cout << "Ba-";
        s = c_stub->Gucci(&context, hb, &rhb);
        if (!s.ok()) {
            std::cout << "No heartbeat response... Is the coordinator alive?" << std::endl;
            return;
        }
        std::cout << "bum" << std::endl;
        sleep(10);
    }
}

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
            client_db[userindex].client_following.push_back(atoi(u_id.c_str()));
        }
        fol_f.close();
    }
    std::ifstream flb_f(std::to_string(username) + "followedBy.txt");
    if(flb_f.good()){
        std::string u_id="";
        while(!flb_f.eof()) {
            std::getline(flb_f, u_id, ',');
            if (flb_f.fail()) break;
            client_db[userindex].client_followers.push_back(atoi(u_id.c_str()));
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
        user_index = find_user(username);
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
        int id = request->username();
        //Upgrade me:
        ClientContext context1;
        Blep b;
        AllUsers au;
        c_stub->GetAllUsers(&context1, b, &au);
        for (auto u : au.users())
        {
            list_reply->add_all_users(u);
        }
        std::ifstream ifs(std::to_string(id)+"followedBy.txt");
        std::string u = "";
        while (ifs.good()){
            std::getline(ifs, u, ',');
            if (u == "" || u == " ") continue;
            list_reply->add_followers(atoi(u.c_str()));
        }

        return Status::OK;
    }

    Status Follow(ServerContext *context, const Request *request, Reply *reply) override
    {
        int username1 = request->username();
        int toFollow = request->arguments(0);
        std::cout << username1 << " is attempting to follow " << toFollow << std::endl;
        int join_index = find_user(toFollow);
        if (join_index < 0 && username1 != toFollow){
            Client *user1 = &client_db[find_user(username1)];
            if (std::find(user1->client_following.begin(), user1->client_following.end(), toFollow) != user1->client_following.end())
            {
                reply->set_msg("you have already joined");
                return Status::OK;
            }
            user1->client_following.push_back(toFollow);
            reply->set_msg("Follow Successful");
            std::ofstream fol_s (std::to_string(username1) + "follows.txt", std::ios::app | std::ios::out | std::ios::in);
            fol_s << toFollow << ",";
            fol_s.close();
            //Update slave
            if (s_stub != nullptr){
                FollowData fi1;
                fi1.set_id(username1);
                std::copy(user1->client_following.begin(), user1->client_following.end(), fi1.mutable_following()->begin());
                std::copy(user1->client_followers.begin(), user1->client_followers.end(), fi1.mutable_followers()->begin());

                Blep b1;
                ClientContext context;

                s_stub->FollowUpdate(&context, fi1, &b1);
            }
        }
        else if (join_index < 0)
            reply->set_msg("unkown user name");
        else
        {
            Client *user1 = &client_db[find_user(username1)];
            Client *user2 = &client_db[join_index];
            if (std::find(user1->client_following.begin(), user1->client_following.end(), toFollow) != user1->client_following.end())
            {
                reply->set_msg("you have already joined");
                return Status::OK;
            }
            user1->client_following.push_back(toFollow);
            user2->client_followers.push_back(username1);
            reply->set_msg("Follow Successful");
            std::ofstream fol_s (std::to_string(username1) + "follows.txt", std::ios::app | std::ios::out | std::ios::in);
            fol_s << toFollow << ",";
            fol_s.close();
            //Update slave
            if (s_stub != nullptr){
                FollowData fi1;
                fi1.set_id(username1);
                std::copy(user1->client_following.begin(), user1->client_following.end(), fi1.mutable_following()->begin());
                std::copy(user1->client_followers.begin(), user1->client_followers.end(), fi1.mutable_followers()->begin());

                FollowData fi2;
                fi2.set_id(toFollow);
                std::copy(user2->client_following.begin(), user2->client_following.end(), fi2.mutable_following()->begin());
                std::copy(user2->client_followers.begin(), user2->client_followers.end(), fi2.mutable_followers()->begin());

                Blep b1, b2;
                ClientContext context1;
                ClientContext context2;

                s_stub->FollowUpdate(&context1, fi1, &b1);
                s_stub->FollowUpdate(&context2, fi2, &b2);
            }
        }
        
        return Status::OK;
    }

    Status Login(ServerContext *context, const Request *request, Reply *reply) override
    {
        login_help(request, reply);

        if (s_stub != nullptr){
            ClientContext cc;
            s_stub->LoginUpdate(&cc, *request, reply);
        }

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

            // Write the current message to "usernametimeline.txt"
            std::string filename = std::to_string(username) + "timeline.txt";
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
                std::ifstream in(std::to_string(username) + "fTimelines.txt");
                int count = 0;
                // Read the last up-to-20 lines (newest 20 messages) from userfTimelines.txt
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
            std::vector<int>::const_iterator it;
            for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
            {
                Client *temp_client = &client_db[find_user(*it)];
                if (temp_client->stream != 0 && temp_client->connected)
                    temp_client->stream->Write(message);
                // For each of the current user's followers, put the message in their following.txt file
                // std::string temp_username = std::to_string(temp_client->username);
                // std::string temp_file = temp_username + "fTimelines.txt";
                // std::ofstream following_file(temp_file, std::ios::app | std::ios::out | std::ios::in);
                // following_file << fileinput;
                // temp_client->following_file_size++;
                // std::ofstream user_file(temp_username + ".txt", std::ios::app | std::ios::out | std::ios::in);
                // user_file << fileinput;
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
        std::string s_login_info = slave_addr + ":" + slave_port;
        s_stub = std::unique_ptr<SNSSandMInform::Stub>(SNSSandMInform::NewStub(grpc::CreateChannel(s_login_info, grpc::InsecureChannelCredentials())));

        std::cout << "Sibling process has made contact" << std::endl;

        return Status::OK;
    }

    Status FollowUpdate(ServerContext *context, const FollowData *request, Blep *response) override
    {
        int id = request->id();
        int u_index = find_user(id);
        std::vector<int> followers;
        auto fllrs = request->followers();
        std::vector<int> following;
        auto fllig = request->following();

        std::copy(fllrs.begin(), fllrs.end(), followers.begin());
        std::copy(fllig.begin(), fllig.end(), following.begin());

        client_db[u_index].client_followers.clear();
        client_db[u_index].client_following.clear();

        for (int s : followers) client_db[u_index].client_followers.push_back(s);
        for (int s : following) client_db[u_index].client_following.push_back(s);

        std::cout << "Updated Following info for " << id << std::endl;

        return Status::OK;
    }

    Status TimelineUpdate(ServerContext *context, const MsgChunk *request, Blep *response) override
    {
        //Shouldn't actually be use din our implementation as we assume that master/slave share a machine
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

void RunServer(std::string port_no)
{
    std::string server_address = "0.0.0.0:" + port_no;
    SNSServiceImpl service;

    //Coordinator Stub
    std::cout << "creating coordinator stub" << std::endl;
    std::string c_login_info = c_hostname + ":" + c_port;
    c_stub = std::unique_ptr<SNSCoord::Stub>(SNSCoord::NewStub(grpc::CreateChannel(c_login_info, grpc::InsecureChannelCredentials())));

    ClientContext context;
    Status s;
    ClusterInfo ci;
    ci.set_addr(my_hostname); 
    ci.set_port(port_no);
    ci.set_id(s_id);
    ci.set_master(isMaster);
    ServerIdent si;
    std::cout << "Attempting to spawn cluster..." << std::endl;
    s = c_stub->ClusterSpawn(&context, ci, &si);
    if (!s.ok()) {
        std::cerr << "Fatal error, server '" << s_id << "' failed to spawn cluster." << std::endl;
    }
    std::cout << "SUCCESS!" << std::endl;
    std::cout << "Beginning heartbeat" << std::endl;
    std::thread t(heartbeat);
    t.detach();
    if (si.addr() != ""){
        //SandMInform stub
        std::string s_login_info = si.addr() + ":" + si.port();
        s_stub = std::unique_ptr<SNSSandMInform::Stub>(SNSSandMInform::NewStub(grpc::CreateChannel(s_login_info, grpc::InsecureChannelCredentials())));

        ClientContext context1;
        ServerIdent sib_id;
        sib_id.set_port(my_port);
        sib_id.set_addr("127.0.0.1"); //For this implementation the address will always be local! Change for multiple machining
        ServerIdent r_id;
        s_stub->PokeMaster(&context1, sib_id, &r_id);
    }

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

    if (argc < 9)
    {
        printUsage();
    }

    my_port = "3010";

    c_hostname = "127.0.0.1";
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
            my_port = argv[i + 1];
            if (my_port.size() > 6)
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
    
    char hostbuff[32];
    int host = gethostname(hostbuff, 32);
    struct hostent *host_entry = gethostbyname(hostbuff);
    char* IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));
    my_hostname = IP;

    RunServer(my_port);

    return 0;
}
