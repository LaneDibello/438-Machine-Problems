#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <thread>
#include <iostream>
#include <stack>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
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
std::unique_ptr<csce438::SNSService::Stub> s_stub = nullptr; //slave/master

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

std::string getTimeStamp(time_t t){
    char buf[32];
    struct tm* tm = localtime(&t);
    strftime (buf, 32, "%Y-%m-%d %H:%M:%S", tm);
    std::string outt = buf;

    return outt;
}

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

Message parseMessage(std::string p, time_t lasttime, bool& good){
    Message msg_l;
    std::string p_un = "";
    std::string p_text = "";
    std::string p_time = p.substr(0, 19);

    std::istringstream msg_chunk(p.substr(23));
    std::getline(msg_chunk, p_un, ':');
    std::getline(msg_chunk, p_text);

    msg_l.set_username(atoi(p_un.c_str()));
    msg_l.set_msg(p_text);
    //google::protobuf::Timestamp *ts = new google::protobuf::Timestamp();
    
    struct tm tm;
    char buf[255];
    memset(&tm, 0, sizeof(tm));
    strptime(p_time.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
    tm.tm_hour -= 1;
    time_t stamp = mktime(&tm);
    good = stamp > lasttime;

    // {
    //     //DEBUG
    //     std::cout << "Stamp was " << ctime(&stamp);
    //     std::cout << "lasttime was" << ctime(&lasttime);
    //     std::cout << "So parsed message was ";
    //     if (good) std::cout << "good!" << std::endl;
    //     else std::cout << "bad!" << std::endl;
    // }

    // ts->set_seconds(stamp);
    // ts->set_nanos(0);
    msg_l.set_timestamp(stamp);

    return msg_l;
}

void catchTimlineUpdates(int id, ServerReaderWriter<Message, Message> *stream){
    //std::ifstream ifs(std::to_string(id) + "fTimelines.txt");
    time_t lasttime = 0;
    for (;;)
    {
        struct stat statbuf;
        if(stat((std::to_string(id)+"fTimelines.txt").c_str(), &statbuf)!=0){
            std::cerr << "Stat failed to find fTimelines info for " << id << std::endl;
            perror("stat");
        }
        time_t last_write = statbuf.st_mtim.tv_sec;
        time_t now = time(NULL);
        double difft = std::difftime(now, last_write);
        if (difft < 2){
            //Do Stuff
            std::cout << "A Timeline update has been detected for " << id << std::endl;
            std::ifstream ifs(std::to_string(id) + "fTimelines.txt");
            if (!ifs.good()){
                std::cerr << "catchTimlineUpdates for " << id << " Failed to open " << id << "fTimelines.txt" << std::endl;
                sleep(1);
                continue;
            }
            std::string p = "";
            bool didwrite = false;
            while (ifs.good()){
                std::getline(ifs, p);
                if (p == "") continue;
                bool b;
                Message m = parseMessage(p, lasttime, b);
                if (!b) continue;
                if (!stream->Write(m)){
                    std::cerr << "Bad write for catchTimeLineUpdates for " << id << std::endl;
                }
                else {
                    //TEMP
                    std::cout << "write to " << id << " Success!" << std::endl;
                }
                didwrite = true;
            }
            if (didwrite) lasttime = time(NULL);
            sleep(2);
            ifs.close();
        }
        sleep(1);
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
    //Client/Server interaction
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
        if (toFollow < 1) {
            std::cerr << "Bad Client id toFollow: " << toFollow << std::endl;
        }
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
                // std::copy(user1->client_following.begin(), user1->client_following.end(), fi1.mutable_following()->begin());
                // std::copy(user1->client_followers.begin(), user1->client_followers.end(), fi1.mutable_followers()->begin());
                fi1.mutable_following()->Add(user1->client_following.begin(), user1->client_following.end());
                fi1.mutable_followers()->Add(user1->client_followers.begin(), user1->client_followers.end());

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

        bool first_time = true;

        while (stream->Read(&message))
        {
            int username = message.username();
            int user_index = find_user(username);
            c = &client_db[user_index];

            if (first_time){
                //Flood Client stream with messages
                std::cout << username << " has open timeline" << std::endl;
                std::stack<Message> msg_stack;
                std::ifstream ifs(std::to_string(username) + "fTimelines.txt");
                if (!ifs.good()){
                    std::ofstream make_file(std::to_string(username) + "fTimelines.txt", std::fstream::app);
                    make_file.close();
                }
                std::string p = "";
                std::string p_un = "";
                std::string p_text = "";
                std::string p_time = "";
                while (ifs.good()){
                    std::getline(ifs, p);
                    if (p == "") continue;
                    p_time = p.substr(0, 20);
                    bool b;

                    Message msg_l = parseMessage(p, 0, b);

                    msg_stack.push(msg_l);
                }
                std::cout << "Writing " << msg_stack.size() << " messages to " << username << std::endl;
                for (int j = 0; j < 20 && !msg_stack.empty(); j++){
                    if(!stream->Write(msg_stack.top())){
                        std::cerr << "Failed to write a message to " << username << std::endl;
                    }
                    msg_stack.pop();
                }
                first_time = false;
                std::thread t(catchTimlineUpdates, username, stream);
                ifs.close();
                t.detach();
            }

            // Write the current message to "usernametimeline.txt"
            std::string filename = std::to_string(username) + "timeline.txt";
            std::ofstream user_file(filename, std::ios::app | std::ios::out | std::ios::in);
            time_t temptime = message.timestamp();
            std::string ttime = getTimeStamp(temptime);
            std::string fileinput = ttime + " :: " + std::to_string(message.username()) + ":" + message.msg() + "\n";
            //"Set Stream" is the default message from the client to initialize the stream
            if (message.msg() != "Set Stream")
                user_file << fileinput;
            // If message = "Set Stream", print the first 20 chats from the people you follow
            
            // Send the message to each follower's stream
            std::vector<int>::const_iterator it;
            for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
            {
                int u_index = find_user(*it);
                if (u_index < 0 || u_index > client_db.size()){
                    std::cerr << "User " << *it << " not in client_db" << std::endl;
                    continue;
                }
                Client *temp_client = &client_db[u_index];
                if (temp_client->stream != 0 && temp_client->connected)
                    temp_client->stream->Write(message);
                
            }
        }
        // If the client disconnected from Chat Mode, set connected to false
        c->connected = false;
        return Status::OK;
    }

    //Master Slave Interaction
    Status PokeMaster(ServerContext *context, const ServerIdent *request, ServerIdent *response) override
    {
        slave_port = request->port();
        slave_addr = request->addr();
        std::string s_login_info = slave_addr + ":" + slave_port;
        s_stub = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(s_login_info, grpc::InsecureChannelCredentials())));

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

        //std::copy(fllrs.begin(), fllrs.end(), followers.begin());
        for (int i : fllrs) {
            followers.push_back(i);
        }
        //std::copy(fllig.begin(), fllig.end(), following.begin());
        for (int i : fllig) {
            followers.push_back(i);
        }

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
    }
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
        //SNSService stub
        std::string s_login_info = si.addr() + ":" + si.port();
        s_stub = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(s_login_info, grpc::InsecureChannelCredentials())));

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
    
    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
  
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    host_entry = gethostbyname(hostbuffer);
    IPbuffer = inet_ntoa(*((struct in_addr*)
                           host_entry->h_addr_list[0]));
  
    //DEBUG
    // printf("Hostname: %s\n", hostbuffer);
    // printf("Host IP: %s", IPbuffer);

    my_hostname = IPbuffer;


    // char hostbuff[32];
    // int host = gethostname(hostbuff, 32);
    // struct hostent *host_entry = gethostbyname(hostbuff);
    // char* IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));
    // my_hostname = IP;

    std::cout << "------------------------------" << std::endl;
    std::cout << "My hostname is " << my_hostname << std::endl;
    std::cout << "------------------------------" << std::endl;

    RunServer(my_port);

    return 0;
}
