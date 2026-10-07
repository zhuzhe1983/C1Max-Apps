#include "pdf.hpp"
#include <atomic>
#include <cstdio>
int main(int argc,char**argv){if(argc!=2)return 2;std::atomic<bool> cancel{false};std::string text,error;
    if(!crosspoint::read_pdf(argv[1],text,error,cancel)){fprintf(stderr,"%s\n",error.c_str());return 1;}
    // Report only counts; don't export text from the user's books.
    size_t nonascii=0;for(unsigned char c:text)if(c>=128)nonascii++;printf("text_bytes=%zu nonascii_bytes=%zu\n",text.size(),nonascii);return 0;
}
