#include "cast_relay.hpp"
#include <iostream>
#include <string>
int main(int argc,char**argv){if(argc!=2)return 2;try{bili::CastRelay relay;std::cout<<relay.start(argv[1],"127.0.0.1")<<std::endl;std::string s;std::getline(std::cin,s);relay.stop();return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
