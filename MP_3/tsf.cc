#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
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
        int fid = request->fid();
        if(client_ids.count(fid)){
            std::ofstream fol_s (std::to_string(fid) + "following.txt", std::ios::app | std::ios::out | std::ios::in);
            fol_s << request->id() << ",";
            fol_s.close();
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
        client_ids.insert(request->id());
        return Status::OK;
    }
};

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