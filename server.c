#define _POSIX_C_SOURCE 200809L
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
#include <sys/socket.h>
#include <sys/inotify.h>


#include "common.c"


typedef struct {
    char filename[1024];
    int file_type;
    int status;
    char content[4096];
}SocketData;



//Global Variables
pthread_t* threadPool;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
Queue *queue;
char *dirname;


void equalize(){

    

}



void* threadFunction(void *arg){

    char buffer[1024];
    int client_sock = 0; 
    int busy = 0;
    int num_bytes;
    SocketData socketData;
            
    while (1)
    {

        if (!busy) 
        {
            //Gets the sock fd of a client
            pthread_mutex_lock(&mutex);

            while (isEmpty(queue))
            {
                pthread_cond_wait(&cond, &mutex);
            }
            
            client_sock = dequeue(queue);
            busy = 1;
            pthread_mutex_unlock(&mutex);

        }

        else /*If thread is already working with a client communication*/
        { 
            

            
                // Get the initial timestamps of all files in the directory
            struct dirent* entry;
            while ((entry = readdir(directory)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;

                char file_path[256];
                snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);

                struct stat file_stat;
                if (stat(file_path, &file_stat) != 0) {
                    perror("stat");
                    closedir(directory);
                    return 1;
                }

                printf("Initial timestamp of file %s: %ld\n", file_path, file_stat.st_mtime);
            }



            // while ((num_bytes = read(client_sock, &socketData, sizeof(socketData))) > 0)
            // {

            //     printf("Filename: %s -", socketData.filename);
            //     printf("File Type: %d -", socketData.file_type);
            //     printf("Content: %ld \n", strlen(socketData.content));

            //     if (strlen(socketData.content) != 0) 
            //     {
            //         copy_file_content();
            //         //TODO: COPY THE FILE CONTENT
            //     }
            //     else
            //     {
            //         create_file();
            //         //TODO: CREATE THE GIVEN FILE
            //     }
                


            // }
        }
        
    }

}


int main(int argc, char *argv[]){

    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    int n;


    if (argc != 4)
    {
        printf("Usage: %s directory threadPoolSize portnumber\n", argv[0]);
        return 1;
    }
    
    char *ip = "127.0.0.1";
    dirname = argv[1];
    int threadPool_size = atoi(argv[2]);
    int port = atoi(argv[3]);
   
    if (threadPool_size < 0)
    {
        printf("Thread size must greater than 0");
        return 1;
    }


    printf("------------------------------------------------\n");
    printf("|              Welcome to BiBakBox             |\n");
    printf("|    Please connect a client to upload files   |\n");
    printf("------------------------------------------------\n");

    //Creates QUEUE that thread can communicate to clients
    queue = createQueue(threadPool_size);

    //Creaties Thread Pool
    threadPool = (pthread_t*)malloc(threadPool_size * sizeof(pthread_t));
    for (size_t i = 0; i < threadPool_size; i++)
    {
        if (pthread_create(&threadPool[i], NULL, threadFunction , NULL) !=0 )
        {
            perror("[-] Create thread ");
            free(threadPool);
            exit(EXIT_FAILURE);
        }
    }
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0)
    {
        perror("[-]Socket Error");
        exit(1);
    }
    printf("[+] TCP server socket created.\n");

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = port;
    server_addr.sin_addr.s_addr = inet_addr(ip);


    n = bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (n < 0)
    {
        perror("[-] Bind error");
        exit(1);
    }
    printf("[+] Bind to the port number: %d\n", port);
    
    listen(server_sock, 5);
    printf("Listening on port %d...\n", port);


    while (1)
    {
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

        char *client_address = inet_ntoa(client_addr.sin_addr);
        printf("[+] Client connected | IP ADRESS: %s |\n",client_address);
    
        pthread_mutex_lock(&mutex);
        enqueue(queue, client_sock);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    
    }
 





    return 0;
}