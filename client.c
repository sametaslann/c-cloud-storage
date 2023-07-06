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
enum Status{ADDED, DELETED, MODIFIED, FINISH_EQUALIZE, S_EXIT, C_EXIT};

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
pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;




FileInfo *files = NULL;
char dirname[PATH_LENGTH];

pthread_t dir_watcher_thread;
pthread_t signal_handler_thread;




int file_count = 0;
sem_t watcher_sem;

// const char *logfd = "logfd.txt";
int logfd;
int signal_received = 0;
int sock;
int lastChangeIndex;
enum Status lastStatus;

void* watcherThreadFunction(void *arg);
void* signal_handler_thread_func(void *arg);

void receiverFunction();
void senderFunction(FileInfo file, enum Status status);
void watch_changes(const char* directory);
void added_new_file_check(const char* directory);
char* remove_parent_dir(char *str);
void delete_file_from_array(char *filename);



int is_file_modified(const FileInfo *file_info);
void writeToLog(const char *filename, enum Status status);
void remove_directory(const char *dir_name);

void cleanup();
void mask_sig(void);
int isSignalReceived();




int main(int argc, char *argv[]){

    struct sockaddr_in addr;
    char logfile[2048];
    


    if (argc < 3 || argc > 4)
    {
        printf("Usage: %s directory portnumber serverIP(optionally)\n", argv[0]);
        return -1;
    }
    mask_sig();

    // if (argc == 3)
    //     strcpy(ip, argv[3]);
    // else
    //     strcpy(ip, "127.0.0.1");

    char *ip = "127.0.0.1";
    strcpy(dirname,argv[1]);
    int port = atoi(argv[2]);
    mkdir(dirname, 0777);

   

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


    snprintf(logfile, PATH_LENGTH*2, "%s/logfile.txt", dirname);

    logfd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0777);

    if (logfd == -1)
    {
        perror("[-] Fopen");
        close(logfd);
        free(files);
        close(sock);
    }
    if (pthread_create(&signal_handler_thread, NULL, signal_handler_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create thread.\n");
        exit(1);
    }

    if (pthread_create(&dir_watcher_thread, NULL, watcherThreadFunction, NULL) != 0) {
        fprintf(stderr, "Failed to create thread.\n");
        exit(1);
    }
    
    sem_init(&watcher_sem, 0, 0); // Initialize watcher_sem with value 1
    receiverFunction(sock);

    close(sock);
    return 0;
}

void cleanup(){


    sem_post(&watcher_sem);

    // printf("SIGNAL JOINED\n");    
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&sig_mutex);
    free(files);
    close(logfd);
    close(sock);


}


void mask_sig(void){
    sigset_t mask;
	sigemptyset(&mask); 
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP); 
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

void* signal_handler_thread_func(void *arg){
    sigset_t mask;
	sigemptyset(&mask);
    siginfo_t info;

    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP); 

    if (sigwaitinfo(&mask, &info) == -1)
    {
        perror("sigwaitinfo");
        return NULL;
    }
    printf("Signal received... Program will terminate...\n");
    pthread_mutex_lock(&sig_mutex);
    signal_received = 1;
    pthread_mutex_unlock(&sig_mutex);


    return NULL;
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




void writeToLog(const char *filename, enum Status status){

    char message[BUFFER_SIZE];

    switch (status)
    {
    case ADDED:
        snprintf(message,BUFFER_SIZE, "%s ADDED\n", filename);
        break;
    
    case DELETED:
        snprintf(message,BUFFER_SIZE, "%s DELETED\n", filename);
        break;

    case MODIFIED:
        snprintf(message,BUFFER_SIZE, "%s MODIFIED\n", filename);
        break;

    case FINISH_EQUALIZE:
        strcpy(message,"--Synchronization ended--\n");
        break;
    default:
        break;
    }
}


void* watcherThreadFunction(void* arg){
    
    SocketData socketData;
    mask_sig();
    sem_wait(&watcher_sem);
    
    while (1)        
    {
        if (isSignalReceived())
             break;   
            
        watch_changes(dirname);
    }
    socketData.status = C_EXIT;
    send(sock, &socketData,  sizeof(SocketData), 0);
    return NULL;
}

void delete_file_from_array(char* filename){


    for (int i = 0; i < file_count; i++)
    {
        if (strcmp(files[i].filename,filename) == 0)
        {
            memmove(&files[i], &files[i + 1], (file_count - i - 1) * sizeof(FileInfo));
            file_count--;
            files = (FileInfo *)realloc(files, file_count * sizeof(FileInfo));
            i--; 
            break;
        }
    }
    
}

void watch_changes(const char* directory){

    added_new_file_check(directory);

    for (int i = 0; i < file_count; i++) {

        
        pthread_mutex_lock(&mutex);
        int returnVal = access(files[i].filename, F_OK);
        pthread_mutex_unlock(&mutex);

        if (returnVal != 0) {

            lastChangeIndex = i;
            lastStatus = DELETED;
            writeToLog(files[i].filename, DELETED);

            pthread_mutex_lock(&mutex);
            senderFunction(files[i], DELETED);
            memmove(&files[i], &files[i + 1], (file_count - i - 1) * sizeof(FileInfo));
            file_count--;
            files = (FileInfo *)realloc(files, file_count * sizeof(FileInfo));
            i--; 
            pthread_mutex_unlock(&mutex);



        }

        else if (is_file_modified(&files[i])) {

            pthread_mutex_lock(&mutex);

            
            if (files[i].file_type != T_DIR)
            {
                lastChangeIndex = i;
                lastStatus = MODIFIED;

                writeToLog(files[i].filename, MODIFIED);
                senderFunction(files[i], MODIFIED);

                stat(files[i].filename, &files[i].last_modified);

            }   
            pthread_mutex_unlock(&mutex);
        }      

    }

    sleep(1); // Sleep for 1 second before checking again

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

    pthread_mutex_lock(&mutex);
    if (stat(file_info->filename, &st) != 0) {
        perror("[-] Stat");
        return 0;
    }
    pthread_mutex_unlock(&mutex);

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
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "logfile.txt") == 0) 
            continue;

        int found = 0;

        for (i = 0; i < file_count; i++) {                    
            snprintf(fullpath, PATH_LENGTH, "%s/%s", directory, entry->d_name);
            
            pthread_mutex_lock(&mutex);

            if (strcmp(fullpath, files[i].filename) == 0) {
                found = 1;
                pthread_mutex_unlock(&mutex);
                break;
            }
            pthread_mutex_unlock(&mutex);


        }

        if (!found) {

            FileInfo file_info;
            snprintf(file_info.filename, PATH_LENGTH, "%s/%s", directory, entry->d_name);

            stat(file_info.filename, &file_info.last_modified);

            if (entry->d_type == DT_DIR)
                file_info.file_type = T_DIR;
            else
                file_info.file_type = T_REG;


            pthread_mutex_lock(&mutex);

            files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
            files[file_count++] = file_info;

            pthread_mutex_unlock(&mutex);



            lastChangeIndex = file_count - 1;
            lastStatus = ADDED;
            writeToLog(file_info.filename, ADDED);
            senderFunction(file_info, ADDED);
            
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

        if (stat(path, &path_stat) != 0)
            continue;



        if (entry->d_type == DT_DIR) {
            remove_directory(path); 
            rmdir(path);
        }
        else
            remove(path);
        
    }

    closedir(dir);

    rmdir(dir_name);
    
}




void senderFunction(FileInfo file, enum Status status){

    SocketData socketData;
    char fullpath[PATH_LENGTH];

    memset(&socketData, 0, sizeof(SocketData));

    strcpy(fullpath, file.filename);
    socketData.status = status;
    socketData.file_type = file.file_type;
    strcpy(socketData.filename , remove_parent_dir(fullpath));

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

    }
    
    else if (status == DELETED);

    
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
        while ((read_bytes = read(fd, socketData.content, BUFFER_SIZE)) > 0) {
            
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



void receiverFunction(){

    SocketData socketData;
    char fullpath[PATH_LENGTH*2 + 2];

    ssize_t received_bytes;
    while ((received_bytes = recv(sock, &socketData, sizeof(SocketData), 0)) > 0) {

        snprintf(fullpath, PATH_LENGTH*2 + 2, "%s/%s", dirname, socketData.filename);
        
        pthread_mutex_lock(&mutex);

        if(socketData.status == ADDED){

            if (lastChangeIndex>0 &&strcmp(files[lastChangeIndex].filename, fullpath) == 0 &&  lastStatus == ADDED);

            else
            {
                
                files = (FileInfo *)realloc(files, (file_count + 1) * sizeof(FileInfo));
                strcpy(files[file_count].filename, fullpath);
                files[file_count].file_type = socketData.file_type;
                stat(files[file_count].filename, &files[file_count].last_modified);
                file_count++;  
                
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
            writeToLog(fullpath, socketData.status);

        }

        

        else if(socketData.status == DELETED){


            delete_file_from_array(fullpath);

            if (socketData.file_type == T_DIR)
                remove_directory(fullpath);                

            else{
                remove(fullpath);
            }    
            writeToLog(fullpath, socketData.status);

        }

        else if(socketData.status == MODIFIED){

            if (lastChangeIndex>0 && strcmp(files[lastChangeIndex].filename, fullpath) == 0 && lastStatus == MODIFIED);

            
            else
            {    

                int fd = open(fullpath, O_WRONLY | O_TRUNC , 0777);
                
                if (fd == -1){
                    perror("[-] Open");
                    exit(EXIT_FAILURE);
                }

                if (strlen(socketData.content)){
                    if(write(fd, socketData.content, BUFFER_SIZE) == -1){
                        perror("[-] Write");
                    }
                }
                

                while (!socketData.doneFlag && ((received_bytes = recv(sock, &socketData, sizeof(SocketData), 0)) > 0)) {
                    
                    if (socketData.doneFlag == 1){
                        break;
                    }

                    else if(write(fd, socketData.content, BUFFER_SIZE) == -1){
                        perror("[-] Write");
                        break;
                    }   
                    memset(socketData.content, 0, BUFFER_SIZE);     

                }
                close(fd);                    
                set_new_timestamp(fullpath);
            writeToLog(fullpath, socketData.status);

            }
        }
        else if(socketData.status == FINISH_EQUALIZE){
            sem_post(&watcher_sem);

        }


        else if(socketData.status == S_EXIT)
        {
            printf("Server exited. Client will be terminated...\n");
            socketData.status = S_EXIT;
            send(sock, &socketData,  sizeof(SocketData), 0);

            pthread_mutex_lock(&sig_mutex);
            signal_received = 1;
            pthread_mutex_unlock(&sig_mutex);
            break;
        }

        else if(socketData.status == C_EXIT)
        {
            printf("Client will be terminated...\n");

            socketData.status = C_EXIT;
            send(sock, &socketData,  sizeof(SocketData), 0);
            pthread_mutex_lock(&sig_mutex);
            signal_received = 1;
            pthread_mutex_unlock(&sig_mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
        memset(&socketData, 0, sizeof(SocketData));     

    }
    cleanup();

}