#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "nfs.h"

// RPC program number and version
#define NFS_PROGRAM 0x21000001 // Replace with your actual program number
#define NFS_VERSION_1 1     // Replace with your actual version number

// RPC procedure numbers
#define LS_PROC 1
#define CREATE_PROC 2
#define DELETE_PROC 3
#define RETRIEVE_FILE_PROC 4
#define SEND_FILE_PROC 5
#define MYNFS_MKDIR_PROC 6
#define MYNFS_OPEN_PROC 7
#define MYNFS_CLOSE_PROC 8
#define MYNFS_READ_PROC 9
#define MYNFS_WRITE_PROC 10
#define MYNFS_OPENDIR_PROC 11
#define MYNFS_READDIR_PROC 12

// Storage for files and directories (for simplicity)
#define MAX_FILES 10
#define MAX_FILENAME_LENGTH 128
#define MAX_FILE_SIZE 1024

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    char data[MAX_FILE_SIZE];
    int size;
} File;

File files[MAX_FILES];
int file_count = 0;

// Service functions
char **ls_1_svc(char **argp, struct svc_req *req) {
    static char *result[MAX_FILES + 1];  // +1 for NULL termination
    printf("ls_1_svc called, file_count: %d\n", file_count);

    for (int i = 0; i < file_count; i++) {
        result[i] = strdup(files[i].filename);
    }
    result[file_count] = NULL;  // Null-terminate the list

    printf("File list terminated with NULL.\n");
    return result;
}

int *create_1_svc(char **argp, struct svc_req *req) {
    static int result;
    if (file_count < MAX_FILES) {
        strncpy(files[file_count].filename, *argp, MAX_FILENAME_LENGTH);
        files[file_count].size = 0;
        file_count++;
        result = 0;  // Success
    } else {
        result = -1;  // Error: storage full
    }
    return &result;
}

int *delete_1_svc(char **argp, struct svc_req *req) {
    static int result;
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].filename, *argp) == 0) {
            for (int j = i; j < file_count - 1; j++) {
                files[j] = files[j + 1];
            }
            file_count--;
            result = 0;  // Success
            return &result;
        }
    }
    result = -1;  // Error: file not found
    return &result;
}

chunk *retrieve_file_1_svc(request *argp, struct svc_req *req) {
    static chunk result;
    result.filename = strdup(argp->filename);
    result.data.data_len = 0;
    result.data.data_val = NULL;
    result.size = 0;
    result.dest_offset = 0;

    int file_index = -1;
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].filename, argp->filename) == 0) {
            file_index = i;
            break;
        }
    }

    if (file_index != -1) {
        FILE *file = fopen(files[file_index].filename, "rb");
        if (file) {
            fseek(file, argp->src_offset, SEEK_SET);
            result.size = argp->size;
            result.dest_offset = argp->dest_offset;
            result.data.data_len = argp->size;
            result.data.data_val = malloc(argp->size);
            fread(result.data.data_val, 1, argp->size, file);
            fclose(file);
        } else {
            fprintf(stderr, "Error: Failed to open file for reading\n");
        }
    } else {
        fprintf(stderr, "Error: File not found\n");
    }
    return &result;
}

int *send_file_1_svc(chunk *argp, struct svc_req *req) {
    static int result;

    FILE *file = fopen(argp->filename, "wb");
    if (file) {
        fseek(file, argp->dest_offset, SEEK_SET);
        fwrite(argp->data.data_val, 1, argp->size, file);
        fclose(file);
        result = 0;  // Success
    } else {
        fprintf(stderr, "Error: Failed to open file for writing\n");
        result = -1;  // Error
    }
    return &result;
}

int *mynfs_mkdir_1_svc(char **argp, struct svc_req *req) {
    static int result;

    if (mkdir(*argp, 0777) == 0) {
        result = 0;  // Success
    } else {
        result = -1;  // Error
    }
    return &result;
}

int *mynfs_open_1_svc(char **argp, struct svc_req *req) {
    static int result;

    FILE *file = fopen(*argp, "rb");
    if (file) {
        fclose(file);  // Close the file if it opened successfully
        result = 0;  // Success
    } else {
        result = -1;  // Error
    }
    return &result;
}

int *mynfs_close_1_svc(char **argp, struct svc_req *req) {
    static int result;

    // No need to open/close a file for this dummy implementation
    result = 0;  // Success
    return &result;
}

chunk *mynfs_read_1_svc(request *argp, struct svc_req *req) {
    return retrieve_file_1_svc(argp, req);  // Delegate to retrieve_file_1_svc
}

int *mynfs_write_1_svc(chunk *argp, struct svc_req *req) {
    return send_file_1_svc(argp, req);  // Delegate to send_file_1_svc
}

int *mynfs_opendir_1_svc(opendir_args *argp, struct svc_req *req) {
    static int result;

    DIR *dir = opendir(argp->dirname);
    if (dir) {
        closedir(dir);  // Close the directory
        result = 0;  // Success
    } else {
        result = -1;  // Error
    }
    return &result;
}

readdir_result *mynfs_readdir_1_svc(readdir_args *argp, struct svc_req *req) {
    static readdir_result result;
    DIR *dir = opendir(argp->dirname);
    if (dir) {
        struct dirent *entry;
        int count = 0;

        while ((entry = readdir(dir)) != NULL) {
            count++;
        }
        rewinddir(dir);

        result.filenames = (char **)malloc((count + 1) * sizeof(char *));
        int i = 0;
        while ((entry = readdir(dir)) != NULL) {
            result.filenames[i] = strdup(entry->d_name);
            i++;
        }
        result.filenames[i] = NULL;  // Null-terminate the list
        result.more = FALSE;

        closedir(dir);
    } else {
        result.filenames = NULL;
        result.more = FALSE;
    }
    return &result;
}

// RPC service dispatcher
void nfs_1(struct svc_req *rqstp, register SVCXPRT *transp) {
    switch (rqstp->rq_proc) {
        case NULLPROC:
            svc_sendreply(transp, (xdrproc_t)xdr_void, NULL);
            return;
        case LS_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_char, (char **)ls_1_svc(NULL, rqstp));
            return;
        case CREATE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, create_1_svc(NULL, rqstp));
            return;
        case DELETE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, delete_1_svc(NULL, rqstp));
            return;
        case RETRIEVE_FILE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_chunk, retrieve_file_1_svc(NULL, rqstp));
            return;
        case SEND_FILE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, send_file_1_svc(NULL, rqstp));
            return;
        case MYNFS_MKDIR_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_mkdir_1_svc(NULL, rqstp));
            return;
        case MYNFS_OPEN_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_open_1_svc(NULL, rqstp));
            return;
        case MYNFS_CLOSE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_close_1_svc(NULL, rqstp));
            return;
        case MYNFS_READ_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_chunk, mynfs_read_1_svc(NULL, rqstp));
            return;
        case MYNFS_WRITE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_write_1_svc(NULL, rqstp));
            return;
        case MYNFS_OPENDIR_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_opendir_1_svc(NULL, rqstp));
            return;
        case MYNFS_READDIR_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_readdir_result, mynfs_readdir_1_svc(NULL, rqstp));
            return;
        default:
            svcerr_noproc(transp);
            return;
    }
}

// Main function to initialize and start the server
int main() {
    // Unset any previous registrations
    pmap_unset(NFS_PROGRAM, NFS_VERSION_1);

    // Create an RPC server handle
    SVCXPRT *transp;
    transp = svcudp_create(RPC_ANYSOCK);
    if (transp == NULL) {
        fprintf(stderr, "Error: Unable to create RPC service.\n");
        exit(1);
    }
    printf("RPC service handle created successfully.\n");

    // Register the service with the RPC server
    if (!svc_register(transp, NFS_PROGRAM, NFS_VERSION_1, nfs_1, IPPROTO_UDP)) {
        fprintf(stderr, "Unable to register (NFS_PROGRAM, NFS_VERSION_1, IPPROTO_UDP).\n");
        exit(1);
    }
    printf("Service registered successfully with program number %d and version %d.\n", NFS_PROGRAM, NFS_VERSION_1);

    // Start the server loop
    printf("Starting svc_run...\n");
    svc_run();  // Start the RPC server loop

    // If svc_run returns, it indicates an error
    fprintf(stderr, "Error: svc_run returned\n");
    exit(1);
}
