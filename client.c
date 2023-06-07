#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <dirent.h>

#include <sys/socket.h>


typedef struct {
    char filename[1024];
    int file_type;
    char content[4096];
}SocketData;


void send_directory_content(int socket, const char *dirname);

int main(int argc, char *argv[]){

    int sock;
    struct sockaddr_in addr;
    socklen_t addr_size;
    char buffer[1024];
    int n;


    if (argc < 3 || argc > 4)
    {
        printf("Usage: %s directory portnumber serverIP(optionally)\n", argv[0]);
        return 1;
    }

    char *ip = "127.0.0.1";
    char *directory = argv[1];
    int port = atoi(argv[2]);
   

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("[-] Socket Error");
        exit(1);
    }
    printf("[+] TCP server socket created.\n");

    memset(&addr, '\0', sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    addr.sin_addr.s_addr = inet_addr(ip);


    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1){
        perror("[-] Connection Error");
        exit(1);
    }
    else{
        printf("Connected Established. \n");

    }


    send_directory_content(sock, directory);

    close(sock);
    return 0;
}


void send_directory_content(int socket, const char *dirname){

    DIR *dir;
    struct dirent *entry;
    SocketData socketData;

    dir = opendir(dirname);
    if (dir == NULL)
    {
        perror("[-] Opendir Error");
        return;
    }

     while ((entry = readdir(dir)) != NULL) {

        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {

            strcpy(socketData.filename, entry->d_name);
            socketData.file_type = entry->d_type;

            write(socket, &socketData, sizeof(socketData));

            // send(socket, entry->d_name, strlen(entry->d_name), 0);

            // send(socket, "\n", 1, 0);
        }


    }

    closedir(dir);
    
    

}