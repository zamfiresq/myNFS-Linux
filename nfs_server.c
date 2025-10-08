#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

#include "nfs.h"

// folder partajat
#define SHARED_DIR "./shared"

// versiune program RPC
#define NFS_PROGRAM 0x21000001
#define NFS_VERSION_1 1

// nr de proceduri
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


// ls_1 scaneaza folderul partajat si returneaza un array de char* (compatibil rpcgen)
char **ls_1_svc(char **argp, struct svc_req *req) {
    static char buffer[1024];
    static char *result;

    DIR *d = opendir(SHARED_DIR);
    struct dirent *dir;
    buffer[0] = '\0';

    if (!d) {
        result = NULL;
        return &result;
    }

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;
        strcat(buffer, dir->d_name);
        strcat(buffer, "\n");
    }
    closedir(d);

    result = buffer;
    return &result;
}

// xdr custom
bool_t xdr_ls_result(XDR *xdrs, char **arr) {
    u_int arr_len = file_count;
    return xdr_array(xdrs, (char **)&arr, &arr_len, MAX_FILES, sizeof(char *), (xdrproc_t)xdr_string);
}

// create_1 verificare NULL 
int *create_1_svc(char **filename, struct svc_req *req) {
    static int result;
    char path[PATH_MAX];

    if (filename == NULL || *filename == NULL || **filename == '\0') {
        fprintf(stderr, "create_1_svc: NULL or empty filename received\n");
        result = -1;
        return &result;
    }

    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, *filename);
    FILE *f = fopen(path, "w");
    if (f) {
        fclose(f);
        result = 0; // succes
    } else {
        perror("create_1_svc fopen");
        result = -1; // eroare
    }
    return &result;
}

// delete_1
int *delete_1_svc(char **argp, struct svc_req *req) {
    static int result;
    char path[PATH_MAX];

    if (argp == NULL || *argp == NULL || **argp == '\0') {
        fprintf(stderr, "delete_1_svc: received NULL or empty filename\n");
        result = -1;
        return &result;
    }

    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, *argp);
    if (remove(path) == 0) {
        printf("delete_1_svc: deleted file %s\n", path);
        result = 0;
    } else {
        perror("delete_1_svc remove");
        result = -1;
    }
    return &result;
}


// retrieve_file_1
chunk *retrieve_file_1_svc(request *argp, struct svc_req *req) {
    static chunk result;

    if (argp == NULL || argp->filename == NULL) {
        fprintf(stderr, "retrieve_file_1_svc: received NULL request or filename\n");
        result.filename = NULL;
        result.data.data_len = 0;
        result.data.data_val = NULL;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, argp->filename);

    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "retrieve_file_1_svc: Failed to open file %s\n", path);
        result.filename = strdup(argp->filename);
        result.data.data_len = 0;
        result.data.data_val = NULL;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    if (fseek(file, argp->src_offset, SEEK_SET) != 0) {
        fprintf(stderr, "retrieve_file_1_svc: Failed to seek in file %s\n", path);
        fclose(file);
        result.filename = strdup(argp->filename);
        result.data.data_len = 0;
        result.data.data_val = NULL;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    result.data.data_val = malloc(argp->size);
    if (!result.data.data_val) {
        fprintf(stderr, "retrieve_file_1_svc: Memory allocation failed\n");
        fclose(file);
        result.filename = strdup(argp->filename);
        result.data.data_len = 0;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    size_t read_bytes = fread(result.data.data_val, 1, argp->size, file);
    fclose(file);

    result.filename = strdup(argp->filename);
    result.data.data_len = read_bytes;
    result.size = read_bytes;
    result.dest_offset = argp->dest_offset;

    return &result;
}



// send_file_1
int *send_file_1_svc(chunk *argp, struct svc_req *req) {
    static int result;
    char path[PATH_MAX];

    if (!argp || !argp->filename || !argp->data.data_val) {
        fprintf(stderr, "send_file_1_svc: invalid arguments\n");
        result = -1;
        return &result;
    }

    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, argp->filename);

    FILE *file = fopen(path, "r+b");
    if (!file) {
        file = fopen(path, "wb");  // dacă nu există, îl creăm
    }

    if (file) {
        fseek(file, argp->dest_offset, SEEK_SET);
        fwrite(argp->data.data_val, 1, argp->size, file);
        fclose(file);
        printf("send_file_1_svc: wrote %d bytes to %s\n", argp->size, path);
        result = 0;
    } else {
        perror("send_file_1_svc fopen");
        result = -1;
    }
    return &result;
}

// mkdir_1_svc
int *mynfs_mkdir_1_svc(char **argp, struct svc_req *req) {
    static int result;
    char path[PATH_MAX];

    if (argp == NULL || *argp == NULL || **argp == '\0') {
        fprintf(stderr, "mynfs_mkdir_1_svc: received NULL or empty dirname\n");
        result = -1;
        return &result;
    }

    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, *argp);

    if (mkdir(path, 0777) == 0) {
        printf("mynfs_mkdir_1_svc: created directory %s\n", path);
        result = 0;  // success
    } else {
        perror("mynfs_mkdir_1_svc mkdir");
        result = -1;  // error
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
    if (argp == NULL) {
        fprintf(stderr, "mynfs_read_1_svc: received NULL request\n");
        static chunk empty_result;
        empty_result.filename = NULL;
        empty_result.data.data_len = 0;
        empty_result.data.data_val = NULL;
        empty_result.size = 0;
        empty_result.dest_offset = 0;
        return &empty_result;
    }
    return retrieve_file_1_svc(argp, req);
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
    static char filenames_buffer[MAX_FILES * MAX_FILENAME_LENGTH];
    static char *file_array[MAX_FILES + 1];

    result.filenames = NULL;
    result.more = FALSE;

    if (!argp || !argp->dirname) {
        fprintf(stderr, "mynfs_readdir_1_svc: received NULL argument\n");
        file_array[0] = NULL;
        result.filenames = (char *)file_array;
        return &result;
    }

    DIR *dir = opendir(argp->dirname);
    if (!dir) {
        fprintf(stderr, "mynfs_readdir_1_svc: failed to open directory %s\n", argp->dirname);
        file_array[0] = NULL;
        result.filenames = (char *)file_array;
        return &result;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        strncpy(&filenames_buffer[count * MAX_FILENAME_LENGTH], entry->d_name, MAX_FILENAME_LENGTH - 1);
        filenames_buffer[count * MAX_FILENAME_LENGTH + MAX_FILENAME_LENGTH - 1] = '\0';
        file_array[count] = &filenames_buffer[count * MAX_FILENAME_LENGTH];
        count++;
    }
    file_array[count] = NULL;

    result.filenames = (char *)file_array;
    result.more = FALSE;

    closedir(dir);
    return &result;
}

// RPC service dispatcher
void nfs_1(struct svc_req *rqstp, register SVCXPRT *transp) {
    switch (rqstp->rq_proc) {
        case NULLPROC:
            svc_sendreply(transp, (xdrproc_t)xdr_void, NULL);
            return;
        case LS_PROC: {
            char *arg = NULL;
            if (!svc_getargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg)) {
                svcerr_decode(transp);
                return;
            }
            char **ls_res = ls_1_svc(&arg, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)ls_res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg);
            return;
        }
        case CREATE_PROC: {
            char *arg = NULL;
            if (!svc_getargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg)) {
                svcerr_decode(transp);
                return;
            }
            int *res = create_1_svc(&arg, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_int, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg);
            return;
        }
        case DELETE_PROC: {
            char *arg = NULL;
            if (!svc_getargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg)) {
                svcerr_decode(transp);
                return;
            }
            int *res = delete_1_svc(&arg, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_int, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg);
            return;
        }
        case RETRIEVE_FILE_PROC: {
            request req = {0};
            if (!svc_getargs(transp, (xdrproc_t)xdr_request, (caddr_t)&req)) {
                svcerr_decode(transp);
                return;
            }
            chunk *res = retrieve_file_1_svc(&req, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_chunk, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_request, (caddr_t)&req);
            return;
        }
        case SEND_FILE_PROC: {
            chunk ch = {0};
            if (!svc_getargs(transp, (xdrproc_t)xdr_chunk, (caddr_t)&ch)) {
                svcerr_decode(transp);
                return;
            }
            int *res = send_file_1_svc(&ch, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_int, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_chunk, (caddr_t)&ch);
            return;
        }
        case MYNFS_MKDIR_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_mkdir_1_svc(NULL, rqstp));
            return;
        case MYNFS_OPEN_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_open_1_svc(NULL, rqstp));
            return;
        case MYNFS_CLOSE_PROC:
            svc_sendreply(transp, (xdrproc_t)xdr_int, mynfs_close_1_svc(NULL, rqstp));
            return;
        case MYNFS_READ_PROC: {
            request req = {0};
            if (!svc_getargs(transp, (xdrproc_t)xdr_request, (caddr_t)&req)) {
                svcerr_decode(transp);
                return;
            }
            chunk *res = mynfs_read_1_svc(&req, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_chunk, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_request, (caddr_t)&req);
            return;
        }
        case MYNFS_WRITE_PROC: {
            chunk ch = {0};
            if (!svc_getargs(transp, (xdrproc_t)xdr_chunk, (caddr_t)&ch)) {
                svcerr_decode(transp);
                return;
            }
            int *res = mynfs_write_1_svc(&ch, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_int, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_chunk, (caddr_t)&ch);
            return;
        }
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
