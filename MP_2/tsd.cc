#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
//#include <google/protobuf/util/status.h>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <map>
#include <queue>
#include <algorithm>
#include <memory>
#include <signal.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;

class user_t {
  public:
    std::string name;
    std::map<std::string, user_t*> following; //maybe user_t* instead
    //std::fstream timeline;
    ServerReaderWriter<Message, Message>* stream;
    bool streambound = false;
    std::vector<std::string> temp_unames;
    bool loggedIn = true;
    
    user_t(std::string un) {
      name = un;
      //following();
      //std::cout << "New client: " << name << std::endl;
      add_follower(this);
      
      //timeline.open ("Timelines/" + name + ".txt", std::ofstream::out | std::ofstream::app);
      
    }
    
    bool has_follower(std::string n){
      return following.count(n);
    }
    
    bool add_follower(user_t* u){
      //std::cout << name << " following " << u->name << std::endl;
      auto ret = following.insert(std::pair<std::string, user_t*>(u->name, u));
      return ret.second;
    }
    
    bool rem_follower(user_t* u){
      if (u->name == name) { return false; }
      //std::cout << name << " unfollowing " << u->name << std::endl;
      return following.erase(u->name);
    }
    
    void post_timeline(std::string msg, std::time_t& time){
      std::string t_str(std::ctime(&time));
      //t_str[t_str.size()-1] = '\0';
      t_str = t_str.substr(0, t_str.size()-1);
      std::string line = name + "(" + t_str + ") >> " + msg;
      std::fstream timeline ("tml_" + name + ".txt", std::ofstream::app);
      timeline << line;
      timeline.close();
      for (auto u : following){
        
        u.second->notify_post(name, msg, t_str);
      }
      
      //std::cout << "msg \"" << line << "\" sent from " << name << std::endl;
      
    }
    
    void notify_post(std::string username, std::string msg, std::string t_str) {
      if (username == name) return;
      std::fstream timeline ("tml_" + name + ".txt", std::ofstream::app);
      if (!streambound) {
        timeline << username << "(" << t_str << ") >> " << msg;
        timeline.close();
        return;
      }
      timeline << username << "(" << t_str << ") >> " << msg << std::endl;
      timeline.close();
      Message m;
      m.set_msg(msg);
      m.set_username(username);
      std::time_t t = std::time(nullptr);
      Timestamp* ts = m.mutable_timestamp();
      *ts = TimeUtil::TimeTToTimestamp(t);
      stream->Write(m);
    }
    
    void close_user(std::ofstream& u_dmp) {
      stream = nullptr;
      streambound = false;
      //timeline.close();
      
      u_dmp << name;
      for (auto i : following){
        u_dmp << ' ' << i.first;
      }
      u_dmp << std::endl;
    }
    
    void grabFollowers(std::map<std::string, user_t*>* db){
      for (auto f : temp_unames){
        add_follower(db->at(f));
      }
      temp_unames.clear();
    }
};

std::map<std::string, user_t*> db;

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
    user_t* req_u;
    try{ req_u = db.at(request->username()); } catch (const std::out_of_range& oor) 
    {
      reply->set_msg("5");
      return Status::OK;
    }
    
    for (auto pu : db){ //all_users
      std::string name = pu.first;
      reply->add_all_users(name);
    }
    
    for (auto fu : req_u->following){ //following users
      std::string name = fu.first;
      reply->add_following_users(name);
    }
    
    reply->set_msg("0");
    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to follow one of the existing
    // users
    // ------------------------------------------------------------
    user_t* req_u;
    user_t* fol_u;
    try{ req_u = db.at(request->username()); } catch (const std::out_of_range& oor) 
    {
      reply->set_msg("5");
      return Status::OK;
    }
    
    try{ fol_u = db.at(request->arguments(0)); } catch (const std::out_of_range& oor)
    {
      reply->set_msg("3");
      return Status::OK;
    }
    
    if (!fol_u->add_follower(req_u))
    {
      reply->set_msg("1");
      return Status::OK;
    }
    
    reply->set_msg("0");
    return Status::OK; 
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to unfollow one of his/her existing
    // followers
    // ------------------------------------------------------------
    
    user_t* req_u;
    user_t* ufl_u;
    try{ req_u = db.at(request->username()); } catch (const std::out_of_range& oor) 
    {
      reply->set_msg("5"); //Error unknown
      return Status::OK;
    }
    
    try{ ufl_u = db.at(request->arguments(0)); } catch (const std::out_of_range& oor)
    {
      reply->set_msg("3"); //Bad username
      return Status::OK;
    }
    
    if (!ufl_u->rem_follower(req_u))
    {
      reply->set_msg("3"); //Not following
      return Status::OK;
    }
    
    reply->set_msg("0");
    return Status::OK; 
    
  }
  
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // a new user and verify if the username is available
    // or already taken
    // ------------------------------------------------------------
    std::string name = request->username();
    user_t* u = new user_t(name);
    auto ret = db.insert(std::pair<std::string, user_t*>(name, u));
    if (!ret.second){
      delete u;
      //return Status::ALREADY_EXISTS;
      if (!db.at(name)->loggedIn) {
        db.at(name)->loggedIn = true;
        return Status::OK;
      }
      return Status::CANCELLED;
    }
    
    return Status::OK;
  }

  Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // receiving a message/post from a user, recording it in a file
    // and then making it available on his/her follower's streams
    // ------------------------------------------------------------
    //Recieve intial message
    Message join;
    stream->Read(&join);
    if (join.msg()[0] != 2){
      return Status::CANCELLED;
    }
    
    try{
      user_t* req_u = db.at(join.username()); //Get connected username
      
      if (!req_u->streambound){ //Bind a stream pointer for it to use
        req_u->stream = stream;
        req_u->streambound = true;
      }
      
      Message m;
      while(stream->Read(&m)){ //Read in coming messages and broadcast it 
        time_t t = TimeUtil::TimestampToTimeT(m.timestamp());
        req_u->post_timeline(m.msg(), t);
      }
      
    } catch (const std::out_of_range& oor) {
      //return Status::UNKNOWN;
        return Status::CANCELLED;
    }
    return Status::OK;
  }

  public:
    static void signal_callback_handler(int signum) {
      std::ofstream user_dump("user.txt");
      for (auto i : db) {
        i.second->close_user(user_dump);
      }
      user_dump.close();
      exit(0);
    }
  
    void parse_dump(){
      std::ifstream user_dump("user.txt");
      while (!user_dump.eof() && !user_dump.fail()) {
        std::string uline;
        std::getline(user_dump, uline);
        std::stringstream ss(uline);
        std::string uname;
        ss >> uname;
        user_t* u = new user_t(uname);
        u->loggedIn = false;
        while (!ss.eof()) {
          std::string f;
          ss >> f;
          u->temp_unames.push_back(f);
        }
        db.insert(std::pair<std::string, user_t*>(uname, u));
      }
      for (auto i : db){
        i.second->grabFollowers(&db);
      }
      user_dump.close();
      std::remove("user.txt");
    }
};



void RunServer(std::string port_no) {
  // ------------------------------------------------------------
  // In this function, you are to write code 
  // which would start the server, make it listen on a particular
  // port number.
  // ------------------------------------------------------------
  std::string server_address("0.0.0.0:"+port_no); //Uh is this the right address?
  SNSServiceImpl service;
  
  service.parse_dump();
  
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  //std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  
  signal(SIGINT, SNSServiceImpl::signal_callback_handler);

  std::string port = "3010";
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
          break;
      default:
	         std::cerr << "Invalid Command Line Argument\n";
    }
  }
  RunServer(port);
  return 0;
}






























