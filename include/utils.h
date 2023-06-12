#ifndef BIBAKBOX_H
#define BIBAKBOX_H

#include <stdio.h>
#include <stdlib.h>

enum Status { ADDED, DELETED, MODIFIED, FINISH_EQUALIZE };
enum FileType { T_DIR, T_REG, T_FIFO };

typedef struct {
    char filename[1024];
    enum FileType file_type;
    enum Status status;
    char content[4096];
    int doneFlag;
} SocketData;

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
} LastFileChange;

void* senderThreadFunction(void *arg);
void* checker_thread_func(void *arg);
int is_file_modified(const FileInfo *file_info);
void save_initial(const char *directory);
void added_new_file_check(const char* directory);
void watch_changes(const char* directory);

#endif /* BIBAKBOX_H */
