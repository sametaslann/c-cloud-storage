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
#include <sys/stat.h>
#include <sys/socket.h>
#include <semaphore.h>


#define BUFFER_SIZE 4096
#define PATH_LENGTH 1024


enum FileType{T_DIR, T_REG, T_FIFO};
enum Status{ADDED, DELETED, MODIFIED, FINISH_EQUALIZE};

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




pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t files_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t que_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t variable_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t que_cond = PTHREAD_COND_INITIALIZER;



FileInfo *files = NULL;
char dirname[PATH_LENGTH];
int file_count = 0;
sem_t watcher_sem;

// const char *logfile = "logfile.txt";
FILE* logFile;

int sock;
int lastChangeIndex;
enum Status lastStatus;

void receive_from_server();
void watch_changes(const char* directory);
void added_new_file_check(const char* directory);
void* dir_watcher_thread_func(void *arg);
void remove_parent_dir(char *str);
int is_file_modified(const FileInfo *file_info);
void send_to_server(FileInfo file, enum Status status);
void writeToLog(const char *filename, enum Status status);
void remove_directory(const char *dir_name);



int main(int argc, char *argv[]){

    struct sockaddr_in addr;
    pthread_t dir_watcher_thread;


    if (argc < 3 || argc > 4)
    {
        printf("Usage: %s directory portnumber serverIP(optionally)\n", argv[0]);
        return -1;
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
    else
        printf("Connected Established. \n");

    logFile = fopen("logfile.txt", "a");

    if (logFile == NULL )
    {
        perror("[-] Fopen");
        fclose(logFile);
        free(files);
        close(sock);
    }
    

    if (pthread_create(&dir_watcher_thread, NULL, dir_watcher_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create thread.\n");
        exit(1);
    }
    
    sem_init(&watcher_sem, 0, 0); // Initialize watcher_sem with value 1
    receive_from_server(sock);

    close(sock);
    return 0;
}


void writeToLog(const char *filename, enum Status status){

    switch (status)
    {
    case ADDED:
        fprintf(logFile, "%s ADDED\n",filename);
        break;
    
    case DELETED:
        fprintf(logFile, "%s DELETED\n",filename);
        break;

    case MODIFIED:
        fprintf(logFile, "%s MODIFIED\n",filename);
        break;

    case FINISH_EQUALIZE:
        fprintf(logFile, "--Synchronization ended--\n");
        break;
    default:
        break;
    }

}


void* dir_watcher_thread_func(void* arg){

    sem_wait(&watcher_sem);
    
    while (1)
        watch_changes(dirname);
}

void watch_changes(const char* directory){

    added_new_file_check(directory);

    for (int i = 0; i < file_count; i++) {


        if (access(files[i].filename, F_OK) != 0) {

            lastChangeIndex = i;
            lastStatus = DELETED;
            // writeToLog(files[i].filename, DELETED);
            send_to_server(files[i], DELETED);

            memmove(&files[i], &files[i + 1], (file_count - i - 1) * sizeof(FileInfo));
            file_count--;
            files = (FileInfo *)realloc(files, file_count * sizeof(FileInfo));
            i--; 
        }

        else if (is_file_modified(&files[i])) {
            
            if (files[i].file_type != T_DIR)
            {
                lastChangeIndex = i;
                lastStatus = MODIFIED;
                // writeToLog(files[i].filename, MODIFIED);
                send_to_server(files[i], MODIFIED);

                stat(files[i].filename, &files[i].last_modified);
            }   
        }        
    }

    sleep(1); // Sleep for 1 second before checking again

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

            
            files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
            files[file_count++] = file_info;


            lastChangeIndex = file_count - 1;
            lastStatus = ADDED;
            // writeToLog(file_info.filename, ADDED);
            send_to_server(file_info, ADDED);
            
        }
        if (entry->d_type == DT_DIR)
        {
            snprintf(fullpath, PATH_LENGTH, "%s/%s", directory, entry->d_name);
            added_new_file_check(fullpath);   //Recursive call
        }
        
    }

    closedir(dir);
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




void send_to_server(FileInfo file, enum Status status){

    SocketData socketData;

    memset(socketData.content, 0, BUFFER_SIZE);
    strcpy(socketData.filename,file.filename);
    socketData.status = status;
    socketData.file_type = file.file_type;
    remove_parent_dir(socketData.filename);

    if (status == ADDED && file.file_type != T_DIR)
    {

        int fd = open(file.filename, O_RDONLY);
        if (fd == -1){
            perror("[-] Open");
            exit(EXIT_FAILURE);
        }


        int sent_bytes = send(sock, &socketData,  sizeof(SocketData), 0);
        if (sent_bytes < 0){
            perror("[-] Error sending data to socket");
            exit(EXIT_FAILURE);
        }


        socketData.status = MODIFIED;
        socketData.doneFlag = 0;
        ssize_t read_bytes;
        while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE) > 0)) {

            if ((int)read_bytes == -1)
                break;

            sent_bytes = send(sock, &socketData,  sizeof(SocketData), 0);

            if (sent_bytes < 0){
                perror("Error sending data to socket");
                break;
            }
        } 
        socketData.doneFlag = 1;
        close(fd); 

        printf("%s ADDED\n", file.filename);
    }
    
    else if (status == DELETED)
    {
        printf("%s DELETED\n", file.filename);

    }
    else if (status == MODIFIED && file.file_type != T_DIR)
    {

        int fd = open(file.filename, O_RDONLY);
        if (fd == -1)
        {
            perror("[-] Open");
            exit(EXIT_FAILURE);
        }
        socketData.doneFlag = 0;

        ssize_t read_bytes;
        while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE) > 0)) {
            
            if ((int)read_bytes == -1)
                break;

            int sent_bytes = send(sock, &socketData, sizeof(SocketData), 0);
            if (sent_bytes < 0){
                perror("Error sending data to socket");
                break;
            }
        }

        socketData.doneFlag = 1; 
        close(fd);
    }

    send(sock, &socketData,  sizeof(SocketData), 0);
    
}

void set_new_timestamp(char *filename){
    for (size_t i = 0; i < file_count; i++)
        if (strcmp(filename, files[i].filename) == 0)
            stat(filename, &files[i].last_modified);            
}



void receive_from_server(){

    SocketData socketData;
    char fullpath[PATH_LENGTH*2];
    mkdir(dirname, 0777);
    

    ssize_t received_bytes;
    while ((received_bytes = recv(sock, &socketData, sizeof(SocketData), 0)) > 0) {

        
        snprintf(fullpath, PATH_LENGTH*2, "%s/%s", dirname, socketData.filename);
        // writeToLog(fullpath, socketData.status);

        if(socketData.status == ADDED){

            if (lastChangeIndex>0 &&strcmp(files[lastChangeIndex].filename, fullpath) == 0 &&  lastStatus == ADDED)
            {
                printf("This file already exists ADDED\n");
                continue;
            }
            else{

                files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
                strcpy(files[file_count].filename, fullpath);
                files[file_count].file_type = socketData.file_type;
                stat(files[file_count].filename, &files[file_count].last_modified);

                
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

                file_count++;  
            }
        }

        

        else if(socketData.status == DELETED){


            if (lastChangeIndex>0 && strcmp(files[lastChangeIndex].filename, fullpath) == 0 && lastStatus == DELETED)
            {
                printf("This file already exists DELETED\n");
                continue;
            }

            else if (socketData.file_type == T_DIR){

                remove_directory(fullpath);

                // if (rmdir(fullpath) == 0) 
                //     printf("directory deleted successfully.\n");
                // else
                //     printf("Unable to delete the directory.\n");
            }

            else                        
            {
                if (remove(fullpath) == 0) 
                    printf("File deleted successfully.\n");
                else 
                    printf("Unable to delete the file.\n");

            }
            
        }

        else if(socketData.status == MODIFIED){

            if (lastChangeIndex>0 && strcmp(files[lastChangeIndex].filename, fullpath) == 0 && lastStatus == MODIFIED)
            {
                printf("This file already exists MODIFIED\n");              

            }
            else{

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
                

                while ((received_bytes = recv(sock, &socketData, sizeof(SocketData), 0)) > 0) {
                    
                    printf("strlen SİZE: %d\n", strlen(socketData.content));
                    if (socketData.doneFlag){
                        break;
                    }

                    else if(write(fd, socketData.content, BUFFER_SIZE) == -1){
                        perror("[-] Write");
                        break;
                    }                
                }
                printf("close\n");
                close(fd);                    

                set_new_timestamp(fullpath);

            }
        }

        else if(socketData.status == FINISH_EQUALIZE){
            printf("FINISH_EQUALIZE\n");
            sem_post(&watcher_sem);
        }
    }
}