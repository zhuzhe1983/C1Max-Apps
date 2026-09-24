#pragma once
#include <string>
#include <vector>

namespace mail {
struct Config {
    std::string pop_host,pop_port="995",smtp_host,smtp_port="465",username,password,sender;
};
struct Message { std::string from,subject,body; unsigned number=0; };
bool receive(const Config &config,std::vector<Message> &messages,std::string &error);
bool send(const Config &config,const std::string &to,const std::string &subject,const std::string &body,std::string &error);
}
