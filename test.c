#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#define MAX_PATH_LENGTH 1024


typedef struct {
    char filename[MAX_PATH_LENGTH];
    struct stat last_modified;
    unsigned char file_type;
} FileInfo;

int file_count = 0;

FileInfo *files = NULL;


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




void watch_directory(const char *directory) {

    save_initial(directory);

    while (1) {
        watch_changes(directory);        
    }

    free(files);
}

int main() {
    const char *directory = "./zort";

    // save_initial(directory);
    watch_directory(directory);

    return 0;
}
