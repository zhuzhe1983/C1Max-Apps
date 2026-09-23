/* BusyBox mv lacks -T; rename(2) atomically replaces a symlink to a directory. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
int main(int argc,char**argv){
    const char*base="/storage/apps/releases/";
    if(argc!=2||strncmp(argv[1],base,strlen(base))||strstr(argv[1],".."))return 2;
    struct stat st;if(lstat("/storage/apps/current",&st)==0&&!S_ISLNK(st.st_mode)){fputs("current must be a symlink\n",stderr);return 2;}
    if(stat(argv[1],&st)||!S_ISDIR(st.st_mode))return 2;
    unlink("/storage/apps/current.new");
    if(symlink(argv[1],"/storage/apps/current.new")||rename("/storage/apps/current.new","/storage/apps/current")){perror("activate");return 1;}
    sync();puts("ACTIVATED");return 0;
}
