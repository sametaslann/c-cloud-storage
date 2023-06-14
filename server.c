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

enum Status{ADDED, DELETED, MODIFIED, FINISH_EQUALIZE};
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
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t files_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t que_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t que_cond = PTHREAD_COND_INITIALIZER;



sem_t semaphore;


Queue *queue;
char *dirname;
int file_count = 0;
FileInfo *files = NULL;
Queue *queue;
LastFileChange lastFileChange;
int total_active_threads = 0;




void* senderThreadFunction(void *arg);
void* checker_thread_func(void *arg);
int is_file_modified(const FileInfo *file_info);
void save_initial(const char *directory);
void added_new_file_check(const char* directory);
void watch_changes(const char* directory);
void set_new_timestamp(char *filename);

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

    if (pthread_create(&checkerThread, NULL, checker_thread_func , NULL) !=0 )
    {
            perror("[-] Create thread ");
            free(threadPool);
            exit(EXIT_FAILURE);
    }
    
    for (size_t i = 0; i < threadPool_size; i++)
    {
        if (pthread_create(&threadPool[i], NULL, senderThreadFunction , NULL) !=0 )
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
    
    listen(server_sock, threadPool_size);

    sem_init(&semaphore, 0, 0); // Initialize semaphore with value 1

    printf("Listening on port %d...\n", port);

    while (1)
    {
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

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


void remove_parent_dir(char *str) {
    size_t keywordLen = strlen(dirname)+1;
    size_t strLen = strlen(str);

    if (keywordLen <= strLen && strncmp(str, dirname, keywordLen-1) == 0) {
        memmove(str, str + keywordLen, strLen - keywordLen + 1);
    }
}


int is_file_modified(const FileInfo *file_info) {
    struct stat st;
    if (stat(file_info->filename, &st) != 0) {
        //perror("[-] Stat");
        return 0;
    }

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

        char path[PATH_LENGTH*2];
        snprintf(path, sizeof(path), "%s/%s", dir_name, entry->d_name);

        if (stat(path, &path_stat) != 0) {
            printf("Failed to get file status: %s\n", path);
            continue;
        }

        printf("%s \n ",entry->d_name);


        if (entry->d_type == DT_DIR) {

            remove_directory(path); 
            int a = rmdir(path);
            printf("return value: %d",a);
        }
        else{
            remove(path);
        }
        
    }

    closedir(dir);

    if (rmdir(dir_name) != 0) {
        printf("Failed to remove directory: %s\n", dirname);
    } else {
        printf("Directory deleted successfully: %s\n", dirname);
    }
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
        snprintf(file_info.filename, PATH_LENGTH, "%s/%s", directory, entry->d_name);
        // printf("%s\n", file_info.filename);

        stat(file_info.filename, &file_info.last_modified);
        files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));

        if (entry->d_type == DT_DIR)
            file_info.file_type = T_DIR;     
        else
            file_info.file_type = T_REG;
        
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


        if (entry->d_type == DT_DIR)
        {
            snprintf(fullpath, PATH_LENGTH, "%s/%s", directory, entry->d_name);
            added_new_file_check(fullpath);   //Recursive call
        }

        int found = 0;


        for (i = 0; i < file_count; i++) {                    
            snprintf(fullpath, PATH_LENGTH, "%s/%s", directory, entry->d_name);
            if (strcmp(fullpath, files[i].filename) == 0) {
                found = 1;
                break;
            }
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


            printf("New file added: %s\n", file_info.filename);

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

    
    for (i = 0; i < file_count; i++) {

        if (is_file_modified(&files[i])) {

            
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

    //Check for new files
    // Check for deleted files
    for (i = 0; i < file_count; i++) {

        if (access(files[i].filename, F_OK) != 0) {
            printf("File deleted: %s\n", files[i].filename);



            pthread_mutex_lock(&mutex);

            strcpy(lastFileChange.filename, files[i].filename);
            lastFileChange.status = DELETED;
            lastFileChange.file_type = files[i].file_type;
            
            pthread_cond_broadcast(&cond);
            pthread_mutex_unlock(&mutex);


            for (int i = 0; i < total_active_threads; i++)
            {
                sem_wait(&semaphore);
            }

            pthread_mutex_lock(&files_mutex);

            // Remove the deleted file from the list
            memmove(&files[i], &files[i + 1], (file_count - i - 1) * sizeof(FileInfo));
            file_count--;
            files = (FileInfo *)realloc(files, file_count * sizeof(FileInfo));
            pthread_mutex_unlock(&files_mutex);

            i--; // Adjust t

        }
    }

    closedir(dir);

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

void equalize_new_client(int client_sock){

    SocketData socketData;


    pthread_mutex_lock(&files_mutex);
    for (int i = 0; i < file_count; i++)
    {
        
        strcpy(socketData.filename,files[i].filename);  
        socketData.file_type = files[i].file_type;
        socketData.status = ADDED;
        socketData.doneFlag = 0;

        remove_parent_dir(socketData.filename);


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
                exit(EXIT_FAILURE);
            }

            
            int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

            if (sent_bytes < 0){
                    perror("[-] Error sending data to socket");
                    exit(EXIT_FAILURE);
            }

            
            
            socketData.status = MODIFIED;
            while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE) > 0)) {

                if ((int)read_bytes == -1)
                    break;

                sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
                if (sent_bytes < 0){
                    perror("[-]Error sending data to socket");
                    break;
                }
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

    socketData.status = FINISH_EQUALIZE;

    int sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
    if (sent_bytes < 0){    
            perror("Error sending data to socket");
            exit(EXIT_FAILURE);
    }



    printf("All files equalized\n");

}


void* receiverThreadFunction(void *arg){
    
    int client_sock = *(int *)arg;
    SocketData socketData;
    ssize_t received_bytes;
    char fullpath[PATH_LENGTH*2];

    
    while ((received_bytes = recv(client_sock, &socketData, sizeof(SocketData), 0)) > 0)
    {

        snprintf(fullpath, PATH_LENGTH*2, "%s/%s", dirname, socketData.filename);

        if (socketData.status == ADDED)
        {

            if (socketData.file_type == T_DIR){
                mkdir(fullpath, 0777);
            }

            else if (socketData.file_type == T_REG)
            {
                printf("Regular file ADDED\n");
                int fd = open(fullpath, O_CREAT, 0777);

                if (fd == -1)
                {
                    perror("[-] Open");
                    exit(EXIT_FAILURE);
                }
                close(fd);    
            }
        }
        if (socketData.status == DELETED)

            if (socketData.file_type == T_DIR){

                remove_directory(fullpath);
            }

            else                        
            {
                if (remove(fullpath) == 0) 
                    printf("File deleted successfully.\n");
                else 
                    printf("Unable to delete the file.\n");

            }
        {


        }
        if (socketData.status == MODIFIED)
        {
            int fd = open(fullpath, O_WRONLY | O_TRUNC , 0777);

            if (fd == -1){
                perror("[-] Open");
                exit(EXIT_FAILURE);
            }

            if (strlen(socketData.content)){
                printf("Content var\n");
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
            printf("close\n");
            // set_new_timestamp(fullpath);
        }
        memset(socketData.content, 0, BUFFER_SIZE);
    }
    return NULL;
}

void set_new_timestamp(char *filename){

    for (size_t i = 0; i < file_count; i++)
        if (strcmp(filename, files[i].filename) == 0)
            stat(filename, &files[i].last_modified);            

}


void* senderThreadFunction(void *arg){

    int client_sock = 0; 
    int busy = 0;
    SocketData socketData;
    pthread_t receiver_thread;


    while (1)
    {
        if (!busy) 
        {
            //Gets the sock fd of a client
            pthread_mutex_lock(&que_mutex);

            while (isEmpty(queue))
            {
                pthread_cond_wait(&que_cond, &que_mutex);
            }
            client_sock = dequeue(queue);

            equalize_new_client(client_sock);

            if (pthread_create(&receiver_thread, NULL, receiverThreadFunction, (void *)&client_sock) != 0) {
                fprintf(stderr, "Failed to create thread.\n");
                exit(1);
            }

            busy = 1;
            pthread_mutex_unlock(&que_mutex);

        }

        else /*If thread is already working with a client communication*/
        { 
            
            pthread_mutex_lock(&mutex);
            pthread_cond_wait(&cond, &mutex);
            pthread_mutex_unlock(&mutex);
            memset(socketData.content, 0, BUFFER_SIZE);

            if (lastFileChange.status == ADDED)
            {   

                if (lastFileChange.file_type == T_DIR)
                {
                    remove_parent_dir(lastFileChange.filename);
                    socketData.status = ADDED;
                    strcpy(socketData.filename,lastFileChange.filename);
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


                    remove_parent_dir(lastFileChange.filename);
                    socketData.status = ADDED;
                    strcpy(socketData.filename,lastFileChange.filename);
                    socketData.file_type = lastFileChange.file_type;

                    
                    int sent_bytes = send(client_sock, &socketData, sizeof(SocketData), 0);

                    if (sent_bytes < 0){
                            perror("[-] Error sending data to socket");
                            exit(EXIT_FAILURE);
                    }

                    
                    
                    socketData.status = MODIFIED;
                    socketData.doneFlag = 0;
                    while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE)) > 0) {
                        printf("Content Size: %d\n", read_bytes);

                        if ((int)read_bytes == -1)
                            break;


                        sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
                    
                        if (sent_bytes < 0){
                            perror("Error sending data to socket");
                            break;
                        }

                    }  

                    socketData.doneFlag = 1;

                    sent_bytes = send(client_sock, &socketData,  sizeof(SocketData), 0);
                    if (sent_bytes < 0){    
                            perror("Error sending data to socket");
                            exit(EXIT_FAILURE);
                    }

                    printf("doneflag gönderildi!\n");
                    close(fd); 

                }
                

                    
            }

            else if (lastFileChange.status == DELETED)
            {
                printf("%s deleted\n", lastFileChange.filename);
                remove_parent_dir(lastFileChange.filename);

                socketData.status = DELETED;
                strcpy(socketData.filename,lastFileChange.filename);
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
                printf("%s MODIFIED\n", lastFileChange.filename);

                int fd = open(lastFileChange.filename, O_RDONLY);
                if (fd == -1)
                {
                    perror("[-] Open");
                    exit(EXIT_FAILURE);
                }

                socketData.status = MODIFIED;
                strcpy(socketData.filename,lastFileChange.filename);
                socketData.file_type = lastFileChange.file_type;
                remove_parent_dir(socketData.filename);
                
                socketData.doneFlag = 0;
                while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE) > 0)) {

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

    if (pthread_join(receiver_thread, NULL) != 0) {
        fprintf(stderr, "Failed to join thread.\n");
        exit(1);
    }

    return NULL;

}