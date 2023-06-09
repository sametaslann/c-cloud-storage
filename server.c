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
#include <dirent.h>
#include <sys/stat.h>


#include "common.c"


#define MAX_PATH_LENGTH 1024

//Structs
typedef struct {
    char filename[1024];
    int file_type;
    int status;
    char content[4096];
}SocketData;

typedef struct {
    char filename[MAX_PATH_LENGTH];
    struct stat last_modified;
    unsigned char file_type;
} FileInfo;



//Global Variables
pthread_t* threadPool;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
Queue *queue;
char *dirname;
int file_count = 0;
FileInfo *files = NULL;
Queue *queue;




void* threadFunction(void *arg);
void* checker_thread_func(void *arg);
int is_file_modified(const FileInfo *file_info);
void save_initial(const char *directory);
void added_new_file_check(const char* directory);
void watch_changes(const char* directory);

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
    pthread_t checkerThread;  
    threadPool = (pthread_t*)malloc(threadPool_size * sizeof(pthread_t));

    if (pthread_create(threadPool, NULL, checker_thread_func , NULL) !=0 )
    {
            perror("[-] Create thread ");
            free(threadPool);
            exit(EXIT_FAILURE);
    }
    
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


int is_file_modified(const FileInfo *file_info) {
    struct stat st;
    if (stat(file_info->filename, &st) != 0) {
        perror("stat");
        return 0;
    }

    return ((st.st_mtime == file_info->last_modified.st_mtime) ? 0 : 1);
}


void save_initial(const char *directory){


    struct dirent *entry;
    DIR *dir;


    dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
            continue;

        FileInfo file_info;
        snprintf(file_info.filename, MAX_PATH_LENGTH, "%s/%s", directory, entry->d_name);
        // printf("%s\n", file_info.filename);

        stat(file_info.filename, &file_info.last_modified);
        file_info.file_type = entry->d_type;
        files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
        files[file_count++] = file_info;

        if (entry->d_type == DT_DIR)
            save_initial(file_info.filename);
    }

    closedir(dir);

}
void added_new_file_check(const char* directory){


    int i;
    struct dirent *entry;
    DIR *dir;
    
    dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    char fullpath[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
            continue;

        int found = 0;

            // printf("%s\n", entry->d_name);

        if (entry->d_type == DT_DIR)
        {
            snprintf(fullpath, MAX_PATH_LENGTH, "%s/%s", directory, entry->d_name);
            added_new_file_check(fullpath);   //Recursive call
        }
        
        for (i = 0; i < file_count; i++) {                    
            snprintf(fullpath, MAX_PATH_LENGTH, "%s/%s", directory, entry->d_name);
            // printf("%s - %s \n", fullpath, files[i].filename);
            if (strcmp(fullpath, files[i].filename) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            FileInfo file_info;
            snprintf(file_info.filename, MAX_PATH_LENGTH, "%s/%s", directory, entry->d_name);
            stat(file_info.filename, &file_info.last_modified);
            files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
            files[file_count++] = file_info;
            printf("New file added: %s\n", file_info.filename);
        }
        
    }


    closedir(dir);
}



void watch_changes(const char* directory){

    int i;
    struct dirent *entry;
    DIR *dir;
    
    dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        return;
    }
    
    for (i = 0; i < file_count; i++) {

        if (is_file_modified(&files[i])) {
            printf("File modified: %s\n", files[i].filename);
            struct stat st;
            stat(files[i].filename, &files[i].last_modified);
        }
    }

    //Check for new files
    // Check for deleted files
    for (i = 0; i < file_count; i++) {
        if (access(files[i].filename, F_OK) != 0) {
            printf("File deleted: %s\n", files[i].filename);

            // Remove the deleted file from the list
            memmove(&files[i], &files[i + 1], (file_count - i - 1) * sizeof(FileInfo));
            file_count--;
            files = (FileInfo *)realloc(files, file_count * sizeof(FileInfo));

            i--; // Adjust the index since we shifted the array
        }
    }

    closedir(dir);

    added_new_file_check(directory);
    sleep(1); // Sleep for 1 second before checking again


}



void *checker_thread_func(void *arg){

    
    char * directory = dirname;
    save_initial(directory);

    while (1) {
        watch_changes(directory);        
    }

    free(files);

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
            // struct dirent* entry;
            // while ((entry = readdir(directory)) != NULL) {
            //     if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            //         continue;

            //     char file_path[256];
            //     snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);

            //     struct stat file_stat;
            //     if (stat(file_path, &file_stat) != 0) {
            //         perror("stat");
            //         closedir(directory);
            //         return 1;
            //     }

            //     printf("Initial timestamp of file %s: %ld\n", file_path, file_stat.st_mtime);
            // }



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