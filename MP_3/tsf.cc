#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <chrono>
#include <string>
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

class SNSFollowerImpl final : public SNSFollower::Service{
    Status Following(ServerContext *context, const FollowPair* request, Blep* response) override {
        int id = request->id();
        if(client_ids.count(id)){
            std::ofstream fol_s (std::to_string(id) + "followedBy.txt", std::ios::app | std::ios::out | std::ios::in);
            fol_s << request->fid() << ",";
            fol_s.close();
        }
        else{
            std::cerr << "Following:\n";
            std::cerr << "Client with ID '" << id << "' is not managed by this follower, ID: '" << f_id << "'\n";
            std::cerr << "Did the coordinator screw up?\n";
            return Status::CANCELLED;
        }
        return Status::OK;
    }
    Status newClient(ServerContext *context, const JoinReq* request, Blep* response) override {
        client_ids.insert(request->id());
        return Status::OK;
    }
};

//Check if uid follows anyone and then tell fid about it
void checkFollowUpdates(int uid, int fid) {
    std::filesystem::path p = std::to_string(uid)+"follows.txt";
    int64_t last_write, now;
    for(;;){
        sleep(30);
        last_write = std::filesystem::last_write_time(p).time_since_epoch().count();
        now = std::chrono::system_clock::now().time_since_epoch().count();
        if (now - last_write < 30000000000){ //if there's been less than 30 seconds since last edit
            //Figure out new follower(s)
            //if we're fid update followedBy.txt
            //otherwise RPC the update to fid
        }
    }
}

void checkTimelineUpdates(int uid){
    //implement me!!!!
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

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Coordinator listening on " << server_address << std::endl;

    server->Wait();
}

int main(int argc, char **argv)
{
    if (argc != 9)
    {
        printUsage();
    }

    std::string port = "3011";

    c_hostname = "";
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

    return 0;
}