#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <map>
#include <string>
#include <stdexcept>
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
// using csce438::Message;
// using csce438::ListReply;
// using csce438::Request;
// using csce438::Reply;
// using csce438::SNSService;
// using csce438::SNSCoord;
// using csce438::ClusterInfo;
// using csce438::JoinReq;
// using csce438::HrtBt;

using namespace csce438;

void checkCluster(struct clustinfo *);

struct clustinfo
{
    std::string master_addr = "";
    std::string master_port = "";

    std::string slave_addr = "";
    std::string slave_port = "";

    bool master_live = false;
    bool slave_live = false;

    unsigned long long mbeats = 0;
    unsigned long long sbeats = 0;

    struct flwr* follower = nullptr;

    std::string print()
    {
        return master_addr + ":" + master_port + " | " + slave_addr + ":" + slave_port;
    }
};

struct flwr
{
    std::string addr = "";
    std::string port = "";
    struct clustinfo * cif = nullptr;
    std::vector<int> client_ids;
    int id; //back-up id
};

// Maps server IDs to clusters
std::map<int, struct clustinfo *> c_map;

//Maps follower IDs to followers
std::map<int, struct flwr *> f_map;

//Maps Client IDs to followers
std::map<int, struct flwwr *> l_map;

class SNSCoordImpl final : public SNSCoord::Service
{
    // server pings the coordinator with new cluster info
    Status ClusterSpawn(ServerContext *context, const ClusterInfo *request, ServerIdent *response) override
    {
        int id = request->id(); // Grab the server ID (used as a key for c_map)
        struct clustinfo *cif;
        try
        { // If the obj already exists, then this is the slave
            cif = c_map.at(id);
            cif->slave_addr = request->addr();
            cif->slave_port = request->port();
            cif->slave_live = true;
            response->set_addr(cif->master_addr);
            response->set_port(cif->master_port);
            response->set_master(false);
        }
        catch (const std::out_of_range &oor) // Otherwise, make a new obj for the master
        {
            cif = new clustinfo;
            cif->master_addr = request->addr();
            cif->master_port = request->port();
            cif->master_live = true;
            c_map[id] = cif;

            std::thread t(checkCluster, cif); // start waiting for heartbeat if it's the master
            t.detach();
            response->set_master(true);
        }
        
        return Status::OK;
    }

    Status GetConnection(ServerContext *context, const JoinReq *request, ClusterInfo *response) override
    {
        int cid = request->id();
        int serverID = (cid % 3) + 1;
        try
        {
            struct clustinfo *cif = c_map.at(serverID);
            response->set_id(serverID);
            if (cif->master_live)
            {
                response->set_addr(cif->master_addr);
                response->set_port(cif->master_port);
            }
            else
            {
                response->set_addr(cif->slave_addr);
                response->set_port(cif->slave_port);
            }

            cif->follower->client_ids.push_back(cid);
            l_map[cid] = cif->follower;

            return Status::OK;
        }
        catch (const std::out_of_range &oor)
        {
            std::cerr << "FATAL error, non-existant server cluster for ID: " << serverID << std::endl;
            std::cerr << "map had:\n";
            for (auto p : c_map)
            {
                std::cerr << p.first << " => " << p.second->print() << std::endl;
            }
            return Status::CANCELLED;
        }
    }

    Status Gucci(ServerContext *context, const HrtBt *request, HrtBt *response) override
    {
        int id = request->id();
        bool isMaster = request->master();

        try
        {
            // Try to find the cluster in the hash map
            struct clustinfo *cif = c_map.at(id);
            // Increment the heatbeat clock for the respective process
            if (isMaster)
                cif->mbeats++;
            else
                cif->sbeats++;
        }
        catch (const std::out_of_range &oor)
        {
            std::cerr << "A bad heartbeat was recieved, with id: " << id << std::endl;
            if (isMaster)
                std::cerr << "Claiming to be Master." << std::endl;
            else
                std::cerr << "Claiming to be Slave." << std::endl;
            return Status::CANCELLED;
        }

        // Consider sticking something in the response??

        return Status::OK;
    }

    Status GetFollowing(ServerContext *context, const JoinReq *request, FollowerInfo *response) override
    {
        int cid = request->id();
        try{
            struct flwr *f = l_map[cid];

            response->set_addr(f->addr);
            response->set_port(f->port);
            response->set_id(f->id);
            response->set_sid(cid);
        }
        catch (const std::out_of_range& oor){
            std::cerr << "GetFollowing:\n"
            std::cerr << "Bad client id '" << cid << std::endl;
            return Status::CANCELLED;
        }
        return Status::OK;
    }

    Status FollowerSpawn(ServerContext *context, const FollowerInfo* request, Blep* response) override
    {
        try{
            struct flwr *f = new flwr;

            f->id = request->id();
            f->addr = request->addr();
            f->port = request->port();
            f->cif = c_map.at(request->sid());

            f_map[f->id] = f;

            f->cif->follower = f;
        }
        catch (const std::out_of_range& oor){
            std::cerr << "FollowerSpawn:\n"
            std::cerr << "Bad server id '" << request->sid() << "' sent from follower '" << f->id << "'" << std::endl;
            std::cerr << "Addr was: " << f->addr << std::endl;
            std::cerr << "Port was: " << f->port << std::endl;
            response->set_dope(false);
            return Status::CANCELLED;
        }
        response->set_dope(true);
        return Status::OK;
    }
};

void checkCluster(struct clustinfo *cif)
{
    unsigned long long mbeats = 0; // Master heart beats
    unsigned long long sbeats = 0; // Slave * *
    for (;;)
    {
        sleep(10); // Should be a beat every 10 sec

        //MASTER CHECK
        if (!cif->master_live && cif->mbeats > 0)
            cif->master_live = true; // ressurection
        if (mbeats - cif->mbeats > 2)
        { // if we miss 2 there's a failure
            // He's dead Jim...
            mbeats = 0;
            cif->mbeats = 0;
            cif->master_live = false;
        }
        else if (cif->master_live)
        { // If we're alive, we need to add a beat
            mbeats++;
        }

        //SLAVE CHECK
        if (!cif->slave_live && cif->sbeats > 0)
            cif->slave_live = true;
        if (sbeats - cif->sbeats > 2)
        {
            // He's dead Jim...
            sbeats = 0;
            cif->sbeats = 0;
            cif->slave_live = false;
        }
        else if (cif->slave_live)
        {
            sbeats++;
        }
    }
}

void RunServer(std::string port_no)
{
    std::string server_address = "0.0.0.0:" + port_no;
    SNSCoordImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Coordinator listening on " << server_address << std::endl;

    server->Wait();
}

void printUsage(std::string arg = "")
{
    if (arg != "")
    {
        std::cerr << "Bad argument: " << arg << std::endl;
    }
    std::cerr << "Usage:" << std::endl;
    std::cerr << "$./coordinator -p <portNum>" << std::endl;

    exit(1);
}

int main(int argc, char **argv)
{

    if (argc != 3)
    {
        printUsage();
    }

    std::string port = "3011";

    for (int i = 1; i < argc; i++)
    {
        std::string arg(argv[i]);

        if (argc == i + 1)
        {
            printUsage(arg);
        }
        else if (arg == "-p")
        {
            port = argv[i + 1];
            if (port.size() > 6)
                printUsage(arg);
            i++;
        }
    }

    // Run RPC server shit
    RunServer(port);

    return 0;
}
