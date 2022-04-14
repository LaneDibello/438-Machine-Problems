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

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
// using csce438::Message;
// using csce438::ListReply;
// using csce438::Request;
// using csce438::Reply;
// using csce438::SNSService;

using namespace csce438;
//Coord info
std::string c_hostname;
std::string c_port;

//Meta Follower info
int f_id;

void printUsage(std::string arg = ""){
  if (arg != "") {std::cerr << "Bad argument: " << arg << endl;}
  std::cerr << "Usage:" << std::endl;
  std::cerr << "$./synchronizer -cip <coordinatorIP> -cp <coordinatorPort> -p <portNum> -id <synchronizerId>" << std::endl;

  exit(1);  
}


int main(int argc, char** argv) {
    
  if (argc != 9) {
    printUsage();
  }
  
  std::string port = "3011";
  
  c_hostname = "";
  c_port = "";
  f_id = -1;
  
  for(int i = 1; i < argc; i++){
    std::string arg(argv[i]);
    
    if (argc == i+1) {
      printUsage(arg);
    }
    
    if (arg == "-cip"){
      c_hostname = argv[i+1];
    }
    else if (arg == "-cp"){
      c_port = argv[i+1];
      if (c_port.size() > 6) printUsage(arg);
    }
    else if (arg == "-p"){
      port = argv[i+1];
      if (port.size() > 6) printUsage(arg);
    }
    else if (arg == "-id"){
      f_id = atoi(argv[i+1]);
      if (f_id < 0) {
        printUsage(arg);
      }
    }
  }
    
    
  return 0;
}