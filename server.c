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
#include <fcntl.h>
#include <semaphore.h>

#include "common.c"

enum Status{ADDED, DELETED, MODIFIED, FINISH_EQUALIZE, S_EXIT, C_EXIT};
enum FileType{T_DIR, T_REG, T_FIFO};

#define PATH_LENGTH 1024
#define BUFFER_SIZE 4096

//Structs
typedef struct {
    char filename[PATH_LENGTH];
    enum FileType file_type;
    enum Status status;
    char content[BUFFER_SIZE];
    int doneFlag;
}SocketData;

typedef struct {
    char filename[PATH_LENGTH];
    struct stat last_modified;
    enum FileType file_type;
} FileInfo;

typedef struct {
    char filename[1024];
    enum FileType file_type;
    enum Status status;
    int fd;
}LastFileChange;

//Global Variables
pthread_t* threadPool;
pthread_t checkerThread;  
pthread_t signalHandlerThread;  
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t files_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t que_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t que_cond = PTHREAD_COND_INITIALIZER;


sem_t semaphore;
Queue *queue;
char *dirname;
int file_count = 0;
FileInfo *files = NULL;
int *exitedClients = NULL;
int exited_clients_count = 0;

Queue *queue;
LastFileChange lastFileChange;
int total_active_threads = 0;
int signal_received = 0;
int server_sock;
int threadPool_size;





void* sender_thread_function(void *arg);
void* watcher_thread_function(void *arg);
void* receiver_thread_function(void *arg);
void* signal_handler_thread_func(void *arg);
void save_initial(const char *directory);
void added_new_file_check(const char* directory);
void watch_changes(const char* directory);
void set_new_timestamp(char *filename);
int is_file_modified(const FileInfo *file_info);

char* remove_parent_dir(char *str);


void cleanup();
void mask_sig(void);
int isSignalReceived();


int main(int argc, char *argv[]){

    int client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    int n;

    mask_sig();

    if (argc != 4)
    {
        printf("Usage: %s directory threadPoolSize portnumber\n", argv[0]);
        return 1;
    }
    
    char *ip = "127.0.0.1";
    dirname = argv[1];
    threadPool_size = atoi(argv[2]);
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
    exitedClients = (int *)malloc(threadPool_size * sizeof(int));
    mkdir(dirname, 0777);


    //Creaties Thread Pool


    threadPool = (pthread_t*)malloc(threadPool_size * sizeof(pthread_t));

    if (pthread_create(&checkerThread, NULL, watcher_thread_function , NULL) !=0 )
    {
            perror("[-] Create thread ");
            free(threadPool);
            exit(EXIT_FAILURE);
    }


    if (pthread_create(&signalHandlerThread, NULL, signal_handler_thread_func , NULL) !=0 )
    {
            perror("[-] Create thread ");
            free(threadPool);
            exit(EXIT_FAILURE);
    }
    
    
    for (size_t i = 0; i < threadPool_size; i++)
    {
        if (pthread_create(&threadPool[i], NULL, sender_thread_function , NULL) !=0 )
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
        cleanup();
        exit(EXIT_FAILURE);
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
        

    }
    printf("[+] Bind to the port number: %d\n", port);
    
    listen(server_sock, threadPool_size);

    sem_init(&semaphore, 0, 0); // Initialize semaphore with value 1

    printf("Listening on port %d...\n", port);

    while (1)
    {
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

        if (isSignalReceived()){
            cleanup();
            break;
        }
        
        char *client_address = inet_ntoa(client_addr.sin_addr);
        printf("[+] Client connected | IP ADRESS: %s |\n",client_address);
    
        pthread_mutex_lock(&que_mutex);
        ++total_active_threads;
        enqueue(queue, client_sock);
        pthread_cond_signal(&que_cond);
        pthread_mutex_unlock(&que_mutex);
    
    }

    return 0;
}

void cleanup(){

    pthread_mutex_lock(&sig_mutex);
    signal_received = 1;    
    pthread_mutex_unlock(&sig_mutex);


    pthread_mutex_lock(&que_mutex);
    pthread_cond_broadcast(&que_cond);
    pthread_mutex_unlock(&que_mutex);


    pthread_mutex_unlock(&mutex);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);


    if (pthread_join(checkerThread, NULL) != 0)
        fprintf(stderr, "Failed to join checkerThread thread.\n");


    if (pthread_join(signalHandlerThread, NULL) != 0) 
        fprintf(stderr, "Failed to join signalHandlerThread thread.\n");

    for (int i = 0; i < threadPool_size; i++)
    {
        sem_post(&semaphore);
        if (pthread_join(threadPool[i], NULL) != 0)
            fprintf(stderr, "Failed to join  threadpool thread.\n");
    }


    
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&sig_mutex);
    pthread_mutex_destroy(&files_mutex);
    pthread_mutex_destroy(&que_mutex);
    free(threadPool);
    free(queue->array);
    free(queue);
    free(files);
    free(exitedClients);
    close(server_sock);

}
int isSignalReceived(){

    pthread_mutex_lock(&sig_mutex);
    if (signal_received){
        pthread_mutex_unlock(&sig_mutex);
        
        return 1;
    }

    pthread_mutex_unlock(&sig_mutex);
    return 0;
}


void mask_sig(void){
    sigset_t mask;
	sigemptyset(&mask); 
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP); 
    sigaddset(&mask, SIGPIPE); 

    pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

void* signal_handler_thread_func(void *arg){
    sigset_t mask;
	sigemptyset(&mask);
    siginfo_t info;

    sigaddset(&mask, SIGINT);
    // sigaddset(&mask, SIGTSTP); 

    if (sigwaitinfo(&mask, &info) == -1)
    {
        perror("sigwaitinfo");
        return NULL;
    }
    printf("Signal received... Program will terminate...\n");
    
    signal_received = 1;
    shutdown(server_sock, SHUT_RD);
    pthread_mutex_unlock(&sig_mutex);


    return NULL;
}


char* remove_parent_dir(char *str) {

    int len = strlen(dirname) +1;
    char *ptr = strstr(str, dirname);
    

    if (ptr != NULL) {
        memmove(ptr, ptr + len, strlen(ptr + len) + 1);
        return ptr;
    }
    else
        return str;
}


int is_file_modified(const FileInfo *file_info) {
    struct stat st;

    pthread_mutex_lock(&files_mutex);

    if (stat(file_info->filename, &st) != 0) {
        //perror("[-] Stat");
        return 0;
    }
    pthread_mutex_unlock(&files_mutex);

    return ((st.st_mtime == file_info->last_modified.st_mtime) ? 0 : 1);
}


void remove_directory(const char *dir_name){

     DIR* dir;
    struct dirent* entry;
    struct stat path_stat;

    dir = opendir(dir_name);
    if (dir == NULL) {
        printf("Failed to open directory: %s\n", dir_name);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char path[PATH_LENGTH*2+2];
        snprintf(path, sizeof(path), "%s/%s", dir_name, entry->d_name);

        if (stat(path, &path_stat) != 0) {
            printf("Failed to get file status: %s\n", path);
            continue;
        }

        if (entry->d_type == DT_DIR) {

            remove_directory(path); 
            rmdir(path);
        }
        else{
            remove(path);
        }
        
    }

    closedir(dir);
    rmdir(dir_name);
}





void save_initial(const char *directory){


    struct dirent *entry;
    DIR *dir;


    dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        cleanup();
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ) 
            continue;

        FileInfo file_info;

         if (entry->d_type == DT_DIR)
            file_info.file_type = T_DIR;     
        else if(entry->d_type == DT_REG)
            file_info.file_type = T_REG;
        else
            continue;


        snprintf(file_info.filename, PATH_LENGTH, "%s/%s", directory, entry->d_name);
        stat(file_info.filename, &file_info.last_modified);
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
        perror("[-] Opendir");
        return;
    }

    char fullpath[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) 
            continue;


        int found = 0;
        for (i = 0; i < file_count; i++) {                    
            snprintf(fullpath, PATH_LENGTH, "%s/%s", directory, entry->d_name);

            pthread_mutex_lock(&files_mutex);
            if (strcmp(fullpath, files[i].filename) == 0) {
                found = 1;
                pthread_mutex_unlock(&files_mutex);   
                break;
            }
            pthread_mutex_unlock(&files_mutex);
        }

        if (!found) {

            FileInfo file_info;
            snprintf(file_info.filename, PATH_LENGTH, "%s/%s", directory, entry->d_name);

            stat(file_info.filename, &file_info.last_modified);

            if (entry->d_type == DT_DIR)
                file_info.file_type = T_DIR;
            else
                file_info.file_type = T_REG;

            
            pthread_mutex_lock(&files_mutex);
            files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
            files[file_count++] = file_info;
            pthread_mutex_unlock(&files_mutex);



            pthread_mutex_lock(&mutex);
            strcpy(lastFileChange.filename, file_info.filename);
            lastFileChange.status = ADDED;
            lastFileChange.file_type = file_info.file_type;
            pthread_cond_broadcast(&cond);
            pthread_mutex_unlock(&mutex);


            //should wait here with semaphore
            for (int i = 0; i < total_active_threads; i++)
                sem_wait(&semaphore);
            
        }

        if (entry->d_type == DT_DIR)
        {
            snprintf(fullpath, PATH_LENGTH, "%s/%s", directory, entry->d_name);
            added_new_file_check(fullpath);   //Recursive call
        }
       
        
    }


    closedir(dir);
}



void watch_changes(const char* directory){

    int i;
    DIR *dir;
    
    dir = opendir(directory);
    if (dir == NULL) {
        perror("opendir");
        return;
    }


    added_new_file_check(directory);

    // Check for deleted files
    for (i = 0; i < file_count; i++) {


        pthread_mutex_lock(&files_mutex);

        if (access(files[i].filename, F_OK) != 0) {

            pthread_mutex_lock(&mutex);

            strcpy(lastFileChange.filename, files[i].filename);
            lastFileChange.status = DELETED;
            lastFileChange.file_type = files[i].file_type;
            
            pthread_cond_broadcast(&cond);
            pthread_mutex_unlock(&mutex);


            for (int i = 0; i < total_active_threads; i++)
                sem_wait(&semaphore);
            


            // Remove the deleted file from the list
            memmove(&files[i], &files[i + 1], (file_count - i - 1) * sizeof(FileInfo));
            file_count--;
            files = (FileInfo *)realloc(files, file_count * sizeof(FileInfo));

            i--; // Adjust t

        }
        pthread_mutex_unlock(&files_mutex);

    }

    
    for (i = 0; i < file_count; i++) {

        if (files[i].file_type != T_DIR && is_file_modified(&files[i])) {

            
            pthread_mutex_lock(&mutex);
            strcpy(lastFileChange.filename, files[i].filename);
            lastFileChange.status = MODIFIED;
            lastFileChange.file_type = files[i].file_type;
            pthread_cond_broadcast(&cond);
            pthread_mutex_unlock(&mutex);


            for (int i = 0; i < total_active_threads; i++)
                sem_wait(&semaphore);

            stat(files[i].filename, &files[i].last_modified);
        }
    }

    

    closedir(dir);

    sleep(1); // Sleep for 1 second before checking again


}



void *watcher_thread_function(void *arg){

    
    char * directory = dirname;

    mask_sig();
    save_initial(directory);
    while (1) {
        if (isSignalReceived())
            break;
        watch_changes(directory);        
    }

    return NULL;
}

void equalize_new_client(int client_sock){

    SocketData socketData;

    pthread_mutex_lock(&files_mutex);
    for (int i = 0; i < file_count; i++)
    {
        char fullpath[PATH_LENGTH];

        memset(socketData.content, 0, BUFFER_SIZE);
        memset(&socketData, 0, sizeof(SocketData));

        if (isSignalReceived())
            break;

        strcpy(fullpath,files[i].filename);  
        strcpy(socketData.filename,remove_parent_dir(fullpath));  
        socketData.file_type = files[i].file_type;
        socketData.status = ADDED;
        socketData.doneFlag = 0;
        

        if (files[i].file_type == T_DIR)
        {
            int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

            if (sent_bytes < 0){
                    perror("Error sending data to socket");
                    break;
            }
        }
        else if (files[i].file_type == T_REG)
        {

            ssize_t read_bytes;
            int fd = open(files[i].filename, O_RDONLY);
            if (fd == -1)
            {
                perror("[-] Open");
                continue;
            }

            int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

            if (sent_bytes < 0){
                    perror("[-] Error sending data to socket");
                    exit(EXIT_FAILURE);
            }

            
            while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE) > 0)) {

                if ((int)read_bytes == -1)
                {
                    break;
                }

                socketData.status = MODIFIED;
                socketData.doneFlag = 0;

                sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
                if (sent_bytes < 0){
                    perror("[-]Error sending data to socket");
                    break;
                } 
                memset(socketData.content, 0, BUFFER_SIZE);
            }  

            socketData.doneFlag = 1;

            sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
            if (sent_bytes < 0){    
                    perror("Error sending data to socket");
                    exit(EXIT_FAILURE);
            }

            close(fd); 
            
        }
    }
    
    pthread_mutex_unlock(&files_mutex);

    memset(&socketData, 0, sizeof(SocketData));
    socketData.status = FINISH_EQUALIZE;
    int sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
    if (sent_bytes < 0){    
            perror("Error sending data to socket");
            exit(EXIT_FAILURE);
    }

    printf("Client synchronized.\n");

}

void* receiver_thread_function(void *arg){
    
    int client_sock = *(int *)arg;
    SocketData socketData;
    ssize_t received_bytes;
    char fullpath[PATH_LENGTH*2+2];

    mask_sig();
    
    while ((received_bytes = recv(client_sock, &socketData, sizeof(SocketData), 0)) > 0)
    {

        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, socketData.filename);

        if (socketData.status == ADDED)
        {

            if (socketData.file_type == T_DIR){
                mkdir(fullpath, 0777);
            }

            else if (socketData.file_type == T_REG)
            {
                int fd = open(fullpath, O_CREAT, 0777);

                if (fd == -1)
                {
                    perror("[-] Open");
                    exit(EXIT_FAILURE);
                }
                close(fd);    
            }
        }
        else if (socketData.status == DELETED)
        {


            if (socketData.file_type == T_DIR)
                remove_directory(fullpath);

            else                        
                if (remove(fullpath) == 0);
        }
        
        
        else if (socketData.status == MODIFIED)
        {
            pthread_mutex_lock(&files_mutex);

            int fd = open(fullpath, O_WRONLY | O_TRUNC , 0777);

            if (fd == -1){
                perror("[-] Open");
                exit(EXIT_FAILURE);
            }

            if (strlen(socketData.content)){
                if(write(fd, socketData.content, strlen(socketData.content)) == -1){
                    perror("[-] Write");
                    break;
                }
            }


            while (!socketData.doneFlag && (received_bytes = recv(client_sock, &socketData, sizeof(SocketData), 0)) > 0) {
                if (socketData.doneFlag){
                    break;
                }

                else if(write(fd, socketData.content, strlen(socketData.content)) == -1){
                    perror("[-] Write");
                    break;
                }                
            }
            pthread_mutex_unlock(&files_mutex);

        }
        else if (socketData.status == S_EXIT)
            break;
        

        else if (socketData.status == C_EXIT)
        {
            printf("Client %d disconnected...\n", client_sock);
            socketData.status = C_EXIT;
            send(client_sock, &socketData, sizeof(SocketData), 0);
            close(client_sock);
            break;
        }

        memset(&socketData, 0, BUFFER_SIZE);
    }
    return NULL;
}


void set_new_timestamp(char *filename){

    for (size_t i = 0; i < file_count; i++)
        if (strcmp(filename, files[i].filename) == 0)
            stat(filename, &files[i].last_modified);            

}


void* sender_thread_function(void *arg){

    int client_sock = 0; 
    int busy = 0;
    int exitFlag = 0;
    SocketData socketData;
    pthread_t receiver_thread;

    mask_sig();

    while (1)
    {
        if (!busy) 
        {
            //Gets the sock fd of a client
            pthread_mutex_lock(&que_mutex);

            while (isEmpty(queue) )
            {
                if (isSignalReceived()){
                    exitFlag = 1;
                    break;
                }
                pthread_cond_wait(&que_cond, &que_mutex);
            }
            if (exitFlag){
                pthread_mutex_unlock(&que_mutex);
                break;
            }

            client_sock = dequeue(queue);

            equalize_new_client(client_sock);

            if (pthread_create(&receiver_thread, NULL, receiver_thread_function, (void *)&client_sock) != 0) {
                fprintf(stderr, "Failed to create thread.\n");
                exit(1);
            }

            busy = 1;
            pthread_mutex_unlock(&que_mutex);
            
            

        }

        else /*If thread is already working with a client communication*/
        { 


            //check signals
            if (isSignalReceived()){
                memset(&socketData, 0, sizeof(SocketData));
                socketData.status = S_EXIT;
                send(client_sock, &socketData, sizeof(SocketData), 0);
                break;
            }

            pthread_mutex_lock(&mutex);
            pthread_cond_wait(&cond, &mutex);
            pthread_mutex_unlock(&mutex);


            memset(&socketData, 0, sizeof(SocketData));

            if (isSignalReceived()){
                socketData.status = S_EXIT;
                send(client_sock, &socketData, sizeof(SocketData), 0);
                break;
            }

            if (lastFileChange.status == ADDED)
            {   

                if (lastFileChange.file_type == T_DIR)
                {
                    socketData.status = ADDED;
                    strcpy(socketData.filename,remove_parent_dir(lastFileChange.filename));  
                    socketData.file_type = lastFileChange.file_type;

                    int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

                    if (sent_bytes < 0){
                            perror("Error sending data to socket");
                            break;
                    }
                   
                }
                else if (lastFileChange.file_type == T_REG)
                {

                    ssize_t read_bytes;
                    int fd = open(lastFileChange.filename, O_RDONLY);
                    if (fd == -1)
                    {
                        perror("[-] Open");
                        exit(EXIT_FAILURE);
                    }

                    
                    socketData.status = ADDED;
                    strcpy(socketData.filename,remove_parent_dir(lastFileChange.filename));  
                    socketData.file_type = lastFileChange.file_type;

                    
                    int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

                    if (sent_bytes < 0){
                            perror("[-] Error sending data to socket");
                            exit(EXIT_FAILURE);
                    }

                    
                    
                    socketData.status = MODIFIED;
                    socketData.doneFlag = 0;
                    while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE)) > 0) {

                        if ((int)read_bytes == -1)
                            break;


                        sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
                    
                        if (sent_bytes < 0){
                            perror("Error sending data to socket");
                            break;
                        }
                        memset(socketData.content, 0, BUFFER_SIZE);

                    }  

                    socketData.doneFlag = 1;

                    sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
                    if (sent_bytes < 0){    
                            perror("Error sending data to socket");
                            exit(EXIT_FAILURE);
                    }

                    close(fd); 

                }
                

                    
            }

            else if (lastFileChange.status == DELETED)
            {

                socketData.status = DELETED;
                strcpy(socketData.filename,remove_parent_dir(lastFileChange.filename));  
                socketData.file_type = lastFileChange.file_type;

                int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

                if (sent_bytes < 0){
                    perror("Error sending data to socket");
                    break;
                }

            }

            else if (lastFileChange.status == MODIFIED && lastFileChange.file_type != T_DIR)
            {
                ssize_t read_bytes;

                int fd = open(lastFileChange.filename, O_RDONLY);
                if (fd == -1)
                {
                    perror("[-] Open");
                    exit(EXIT_FAILURE);
                }

                socketData.status = MODIFIED;
                socketData.file_type = lastFileChange.file_type;
                strcpy(socketData.filename,remove_parent_dir(lastFileChange.filename));  

                
                socketData.doneFlag = 0;
                while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE)) > 0) {

                    if ((int)read_bytes == -1)
                        break;


                    int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);
                    if (sent_bytes < 0){
                        perror("Error sending data to socket");
                        break;
                    }
                }

                socketData.doneFlag = 1; 
                int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

                if (sent_bytes < 0){
                    perror("[-]Error sending data to socket");
                }

                close(fd);
            }
            
            sem_post(&semaphore);
        }
        
        
    }

    if (client_sock != 0){
        if(pthread_join(receiver_thread, NULL) != 0){
            fprintf(stderr, "Failed to join thread.\n");
        }
    }

    return NULL;

}