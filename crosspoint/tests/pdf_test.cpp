#include "pdf.hpp"
#include "net.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char**argv){
    assert(argc==2);namespace fs=std::filesystem;fs::path base=argv[1];fs::create_directories(base/"root/crosspoint");fs::create_directories(base/"data/crosspoint");
    setenv("C1_APPS_ROOT",(base/"root").c_str(),1);setenv("C1_APPS_DATA",(base/"data").c_str(),1);
    auto tool=base/"root/crosspoint/c1max-pdftotext",book=base/"book.pdf",calls=base/"calls";
    auto parser=[&](const std::string& body){std::ofstream(tool)<<"#!/bin/sh\necho call >> '"<<calls.string()<<"'\n"<<body;chmod(tool.c_str(),0700);};
    parser("printf 'Hello 中文书目\\n'\n");std::ofstream(book)<<"%PDF-1.4 fixture";
    std::atomic<bool> cancel{false};std::string text,error;
    assert(crosspoint::read_pdf(book,text,error,cancel));assert(text=="Hello 中文书目\n");
    assert(crosspoint::read_pdf(book,text,error,cancel));assert(c1::read_file(calls,100)=="call\n");
    for(auto&e:fs::directory_iterator(base/"data/crosspoint/pdf-cache"))std::ofstream(e.path())<<"broken cache";
    assert(crosspoint::read_pdf(book,text,error,cancel));assert(c1::read_file(calls,100)=="call\ncall\n");
    std::ofstream(book,std::ios::app)<<" changed";parser("printf 'New text\\n'\n");assert(crosspoint::read_pdf(book,text,error,cancel));assert(text=="New text\n");
    // Source identity invalidates cached results; cancelled conversion publishes nothing.
    std::ofstream(book,std::ios::app)<<" again";parser("exec sleep 10\n");auto start=std::chrono::steady_clock::now();
    std::thread stopper([&]{usleep(120000);cancel=true;});assert(!crosspoint::read_pdf(book,text,error,cancel));stopper.join();
    assert(error=="Cancelled"&&text.empty());assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
    cancel=false;parser("printf 'Recovered\\n'\n");assert(crosspoint::read_pdf(book,text,error,cancel));assert(text=="Recovered\n");
    std::ofstream(book,std::ios::app)<<" empty";parser("printf '\\n\\f  '\n");assert(!crosspoint::read_pdf(book,text,error,cancel));assert(error.find("no extractable text")!=std::string::npos);
    parser("exit 2\n");assert(!crosspoint::read_pdf(book,text,error,cancel));assert(error.find("could not read")!=std::string::npos);
    puts("PASS PDF: cache hit, corrupt cache, source invalidation, cancel/reap, empty/failed extraction");
}
