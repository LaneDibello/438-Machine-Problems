#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <algorithm>
#include <iostream>
#include <memory>
#include <thread>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <sys/types.h>
#include <sys/stat.h>
#include <iterator>
#include <string>
#include <stack>
#include <set>
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
using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;

using namespace csce438;

// Coord info
std::string c_hostname;
std::string c_port;

// Meta Follower info
int f_id;
int s_id;
std::set<int> client_ids;
std::string my_hostname;

//Stub
std::unique_ptr<csce438::SNSCoord::Stub> c_stub;

void checkFollowUpdates(int);
void checkTimelineUpdates(int);

class SNSFollowerImpl final : public SNSFollower::Service{
    Status Following(ServerContext *context, const FollowPair* request, Blep* response) override {
        int id = request->id();
        int fid = request->fid();
        if(client_ids.count(fid)){
            std::ofstream fol_s (std::to_string(fid) + "followedBy.txt", std::ios::app | std::ios::out | std::ios::in);
            fol_s << id << ",";
            fol_s.close();
            std::cout << fid << " is followed by " << id << std::endl;
        }
        else{
            std::cerr << "Following:\n";
            std::cerr << "Client with ID '" << fid << "' is not managed by this follower, ID: '" << f_id << "'\n";
            std::cerr << "Did the coordinator screw up?\n";
            return Status::CANCELLED;
        }
        return Status::OK;
    }
    Status newClient(ServerContext *context, const JoinReq* request, Blep* response) override {
        int id = request->id();
        std::cout << "Inserting a new Client: " << id << std::endl;
        if(client_ids.insert(id).second){
            std::thread t1(checkFollowUpdates, id);
            std::thread t2(checkTimelineUpdates, id);
            t1.detach();
            t2.detach();
            return Status::OK;
        }
        else {
            std::cout << "The new client " << id << " already existed" << std::endl;
            return Status::CANCELLED;
        }
        
    }
    Status newMessage(ServerContext *context, const MsgChunk* request, Blep* response) override {
        int recv_id = request->id();
        std::cout << "New Message(s) have arrived for " << recv_id << std::endl;
        if (!client_ids.count(recv_id)) {
            std::cerr << "Message recipiant " << recv_id << " is not managed by this follower" << std::endl;
            std::cerr << "Did the coordinator screw up?\n";
            return Status::CANCELLED;
        }
        //distribute the new messages to the follower
        std::ofstream ofs(std::to_string(recv_id) + "fTimelines.txt", std::ios::app | std::ios::out | std::ios::in);
        for (auto msg : request->msgs()){
            ofs << msg << std::endl;
        }
        ofs.close();
        return Status::OK;
    }
};

//Check if uid follows anyone and then tell their follower about it
void checkFollowUpdates(int uid) {
    std::set<int> following;
    std::ifstream ifs(std::to_string(uid)+"follows.txt");
    if (ifs.fail()){
        std::cout << uid << " doesn't yet have a 'follows.txt'. Creating one..." << std::endl;
            std::ofstream ofs(std::to_string(uid)+"follows.txt");
            ofs.close();
    }
    std::string u = "";
    while(ifs.good()){
        std::getline(ifs, u, ',');
        if (u == "") continue;
        following.insert(atoi(u.c_str()));
    }
    ifs.close();
    sleep(1); //Dirty trick to keep the file creation from breaking things

    for(;;){
        sleep(30);
        struct stat statbuf;
        if(stat((std::to_string(uid)+"follows.txt").c_str(), &statbuf)!=0){
            std::cerr << "Stat failed to find follows info for " << uid << std::endl;
            perror("stat");
            continue;
        }
        time_t last_write = statbuf.st_mtim.tv_sec;
        time_t now = time(nullptr);
        double difft = std::difftime(now, last_write);
        std::cout << difft << " seconds have passed since " << std::to_string(uid)+"follows.txt was last edited" << std::endl;

        if (difft < 30){ //if there's been less than 30 seconds since last edit
            std::cout << "A follower update was detected for " << uid << std::endl;
            //Figure out new follower(s)
            std::set<int> fol;
            std::ifstream ifs(std::to_string(uid)+"follows.txt");
            u = "";
            while(ifs.good()){
                std::getline(ifs, u, ',');
                if (u == "") continue;
                fol.insert(atoi(u.c_str()));
            }
            if (fol.empty()){
                std::cout << "False alarm, no follows found for " << uid << std::endl;
                continue;
            }
            std::set<int> diff;
            std::set_difference (fol.begin(), fol.end(), following.begin(), following.end(), std::inserter(diff, diff.end()));
            
            if (diff.empty()){
                std::cerr << "No new follows were found for " << uid << std::endl;
                continue;
            }

            //Update the following set
            following.clear();
            std::copy(fol.begin(), fol.end(), std::inserter(following, following.end()));

            //Get the followedBy.txt update for each new followed
            for (int i : diff){
                if (client_ids.count(i)){ //If it's one of our update and move on
                    std::ofstream fol_s (std::to_string(i) + "followedBy.txt", std::ios::app | std::ios::out | std::ios::in);
                    fol_s << std::to_string(uid) << ",";
                    fol_s.close();
                }
                else {
                    //RPC coord to figure out who owns this boy
                    JoinReq jr;
                    FollowerInfo ci;
                    ClientContext context;

                    jr.set_id(i);

                    Status status = c_stub->GetFollowing(&context, jr, &ci);
                    if (!status.ok()){
                        std::cerr << "The coordinator did not return info for " << i << "'s synchronizer" << std::endl;
                        continue;
                    }
                    
                    std::cout << "Notifying follower " << ci.id() << " that " << i << " is followed by " << uid << std::endl;

                    //RPC the follower to send the update
                    std::string f_login_info = ci.addr() + ":" + ci.port();
                    auto f_stub = std::unique_ptr<SNSFollower::Stub>(SNSFollower::NewStub(grpc::CreateChannel(f_login_info, grpc::InsecureChannelCredentials())));

                    FollowPair fp;
                    Blep b;
                    ClientContext context1;

                    fp.set_id(uid); //uid follows i
                    fp.set_fid(i);

                    status = f_stub->Following(&context1, fp, &b);
                }
            }
        }
    }
}

//Check if uid updates their timeline, and notify the others
void checkTimelineUpdates(int uid){
    std::ifstream ifs(std::to_string(uid)+"timeline.txt");
    if (ifs.fail()){
        std::cout << uid << " doesn't yet have a 'timeline.txt'. Creating one..." << std::endl;
            std::ofstream ofs(std::to_string(uid)+"timeline.txt");
            ofs.close();
    }
    ifs.close();
    sleep(1); //Dirty trick to keep the file creation from breaking things

    for(;;){
        sleep(30);
        struct stat statbuf;
        if(stat((std::to_string(uid)+"timeline.txt").c_str(), &statbuf)!=0){
            std::cerr << "Stat failed to find timeline info for " << uid << std::endl;
            perror("stat");
            continue;
        }
        time_t last_write = statbuf.st_mtim.tv_sec;
        time_t now = time(nullptr);
        double difft = std::difftime(now, last_write);
        std::cout << difft << "seconds have passed since " << std::to_string(uid)+"timeline.txt was last edited" << std::endl;
        
        if (difft < 30){
            std::cout << "An update was found in " << uid << "'s timeline." << std::endl;
            //Grab the posts
            std::ifstream ifs(std::to_string(uid)+"timeline.txt");
            std::stack<std::string> posts;
            std::string p = "";
            while(ifs.good()){
                std::getline(ifs, p);
                if(p == "") continue;
                posts.push(p);
            }
            ifs.close();

            if (posts.empty()){
                std::cout << "No posts found for " << uid << std::endl;
                continue;
            }

            //Build the MsgChunk
            MsgChunk mc;
            int msgcount = 0;
            //mc.set_id(uid);
            while (!posts.empty()){ //we want most recent messages
                //Time comparsion
                std::string timestamp = posts.top().substr(0, 20);
                char nowbuf[100];
                time_t now = time(0);
                struct tm *nowtm;
                nowtm = localtime(&now);
                nowtm->tm_sec -= 30;
                mktime(nowtm);
                strftime(nowbuf, sizeof(nowbuf), "%Y-%m-%dT%H:%M:%SZ", nowtm);
                if (strncmp(timestamp.c_str(), nowbuf, timestamp.length()) >= 0){
                    mc.add_msgs(posts.top());
                    msgcount++;
                }
                posts.pop();
            }

            //Collect Followers
            std::ifstream ifs1(std::to_string(uid)+"followedBy.txt");
            std::vector<int> followers;
            std::string u = "";
            while(ifs1.good()){
                std::getline(ifs1, u, ',');
                if(u == "") continue;
                followers.push_back(atoi(u.c_str()));
            }
            ifs1.close();

            std::cout << "Broadcasting " << msgcount << " messages to " << followers.size() << " followers." << std::endl;

            //Broadcast our messages
            for (int f : followers){
                ClientContext context;
                std::cout << "Informing follower " << f << " about client " << uid << "'s update." << std::endl;
                //Who does this belong to?
                JoinReq jr;
                FollowerInfo fi;
                jr.set_id(f);
                Status s = c_stub->GetFollowing(&context, jr, &fi);
                if (!s.ok()) continue;

                //RPC the follower to send the update
                std::string f_login_info = fi.addr() + ":" + fi.port();
                auto f_stub = std::unique_ptr<SNSFollower::Stub>(SNSFollower::NewStub(grpc::CreateChannel(f_login_info, grpc::InsecureChannelCredentials())));

                mc.set_id(f);
                Blep b;

                ClientContext context1;
                f_stub->newMessage(&context1, mc, &b);
            }
        }
    }
}

void printUsage(std::string arg = "")
{
    if (arg != "")
    {
        std::cerr << "Bad argument: " << arg << std::endl;
    }
    std::cerr << "Usage:" << std::endl;
    std::cerr << "$./synchronizer -cip <coordinatorIP> -cp <coordinatorPort> -p <portNum> -id <synchronizerId>" << std::endl;

    exit(1);
}

void RunServer(std::string port_no){
    std::string server_address = "0.0.0.0:" + port_no;
    SNSFollowerImpl service;

    //Coordinator Stub
    std::string c_login_info = c_hostname + ":" + c_port;
    c_stub = std::unique_ptr<SNSCoord::Stub>(SNSCoord::NewStub(grpc::CreateChannel(c_login_info, grpc::InsecureChannelCredentials())));

    //Spawn Follower for Coordinator
    ClientContext context;
    FollowerInfo fi;
    fi.set_addr(my_hostname); 
    fi.set_port(port_no);
    fi.set_id(f_id);
    fi.set_sid(s_id);
    Blep b;
    std::cout << "Contacing Coordinator to Spawn Follower..." << std::endl;
    Status s = c_stub->FollowerSpawn(&context, fi, &b);
    if(!s.ok()) {
        std::cerr << "Fatal Error: coudl not spawn follower" << std::endl;
        std::cerr << "c_stub was craeted with: " << c_login_info << std::endl;
        std::cerr << "Follower's port was: " << port_no << std::endl;
    }
    std::cout << "SUCCESS!" << std::endl;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Synchonizer listening on " << server_address << std::endl;

    server->Wait();
}

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        printUsage();
    }

    std::string port = "3011";

    c_hostname = "127.0.0.1";
    c_port = "";
    f_id = -1;

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
            f_id = atoi(argv[i + 1]);
            if (f_id < 0)
            {
                printUsage(arg);
            }
            i++;
        }
    }

    if(f_id < 0) {
        printUsage("-id");
    }
    else{
        s_id = f_id;
    }

    char hostbuff[32];
    int host = gethostname(hostbuff, 32);
    struct hostent *host_entry = gethostbyname(hostbuff);
    char* IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));
    my_hostname = IP;

    RunServer(port);

    return 0;
}