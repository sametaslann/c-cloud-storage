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
#include <fcntl.h>


#include <sys/socket.h>

#define BUFFER_SIZE 4096

enum FileType{T_DIR, T_REG, T_FIFO};

enum Status{ADDED, DELETED, MODIFIED};

typedef struct {
    char filename[1024];
    enum FileType file_type;
    enum Status status;
    char content[4096];
    int doneFlag;
}SocketData;


char dirname[1024];

void send_directory_content(int socket);

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
    strcpy(dirname,argv[1]);
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



    send_directory_content(sock);

    close(sock);
    return 0;
}


void send_directory_content(int socket){

    // DIR *dir;
    struct dirent *entry;
    SocketData socketData;
    char buffer[BUFFER_SIZE];
    // dir = opendir(dirname);
    // if (dir == NULL)
    // {
    //     perror("[-] Opendir Error");
    //     return;
    // }

    chdir(dirname);


    // read(socket, buffer, sizeof(buffer));
    // printf("%s",buffer);



    ssize_t received_bytes;
    while ((received_bytes = recv(socket, &socketData, sizeof(SocketData), 0)) > 0) {

        printf("%s -- \n", socketData.filename);

        if(socketData.status == ADDED){

            if (socketData.file_type == T_DIR)
                mkdir(socketData.filename);



            else if (socketData.file_type == T_REG)
            {
                printf("Regular file ADDED\n");
                int fd = open(socketData.filename, O_CREAT, 0777);

                if (fd == -1)
                {
                    perror("[-] Open");
                    exit(EXIT_FAILURE);
                }
                close(fd);    
            }

            
        }

        else if(socketData.status == DELETED){

            if (socketData.file_type == T_DIR){

                if (rmdir(socketData.filename) == 0) 
                    printf("directory deleted successfully.\n");
                else 
                    printf("Unable to delete the directory.\n");

            }

            else                        
            {
                if (remove(socketData.filename) == 0) 
                    printf("File deleted successfully.\n");
                else 
                    printf("Unable to delete the file.\n");


            }
            
        }

        else if(socketData.status == MODIFIED){

            printf("MODIFIED\n");

            
            int fd = open(socketData.filename, O_WRONLY | O_TRUNC , 0777);

            
            if (fd == -1)
            {
                perror("[-] Open");
                exit(EXIT_FAILURE);
            }

            if (strlen(socketData.content))
            {
                printf("Content var\n");
                if(write(fd, socketData.content, strlen(socketData.content)) == -1){
                    perror("[-] Write");
                    break;
                }
            }
            

            while (!socketData.doneFlag && (received_bytes = recv(socket, &socketData, sizeof(SocketData), 0)) > 0) {
                printf("Doneflag: %d\n", socketData.doneFlag);
                // printf("Received data: %s", socketData.content);
                if (socketData.doneFlag){
                    break;
                }

                else if(write(fd, socketData.content, strlen(socketData.content)) == -1){
                    perror("[-] Write");
                    break;
                }                
            }
            printf("close\n");

            close(fd);                    
        }
    }




    //  while ((entry = readdir(dir)) != NULL) {

    //     if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {

    //         strcpy(socketData.filename, entry->d_name);
    //         socketData.file_type = entry->d_type;

    //         write(socket, &socketData, sizeof(socketData));

    //         // send(socket, entry->d_name, strlen(entry->d_name), 0);

    //         // send(socket, "\n", 1, 0);
    //     }


    // }

    // closedir(dir);
    
    

}