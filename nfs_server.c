#include <errno.h>
#include <stddef.h>
#include <stdio.h>    // pt snprintf
#include <string.h>   // pt strcmp
#include <stdlib.h>
#include <rpc/rpc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>   // pt rmdir
#include "nfs.h"

// folder partajat
#define SHARED_DIR "./shared"


// versiune program RPC
#define NFS_PROGRAM 0x21000001
#define NFS_VERSION_1 1

#define LS_PROC 1
#define CREATE_PROC 2
#define DELETE_PROC 3
#define RETRIEVE_FILE_PROC 4
#define SEND_FILE_PROC 5
#define MYNFS_MKDIR_PROC 6
#define MYNFS_REMDIR_PROC 7
#define MYNFS_READ_PROC 8
#define MYNFS_WRITE_PROC 9
#define MYNFS_READDIR_PROC 11   

#define MAX_FILENAME_LENGTH 128
#define MAX_FILE_SIZE 1024

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    char data[MAX_FILE_SIZE];
    int size;
} File;

File files[MAX_FILES];
int file_count = 0;


// helper pt construirea unui absolute path 
static int make_path(char *path, size_t pathlen, const char *rel) {
    if (!path || pathlen == 0) return -1;
    if (!rel || !*rel || strcmp(rel, ".") == 0) {
        // doar pentru SHARED_DIR
        int n = snprintf(path, pathlen, "%s", SHARED_DIR);
        if (n < 0 || (size_t)n >= pathlen) return -1;
        return 0;
    }
    if (rel[0] == '/')
        rel++;
    int n = snprintf(path, pathlen, "%s/%s", SHARED_DIR, rel);
    if (n < 0 || (size_t)n >= pathlen) return -1;
    return 0;
}


// ls_1 scaneaza directorul cerut relativ la SHARED_DIR
char **ls_1_svc(char **argp, struct svc_req *req) {
    static char buffer[64 * 1024];   // buffer pentru listare
    static char *result;
    char path[PATH_MAX];

    // daca se primeste NULL sau sir gol, folosim .
    const char *sub = (argp && *argp && **argp) ? *argp : ".";

    // calea reala pe server cu make_path
    if (make_path(path, sizeof(path), sub) != 0) {
        result = NULL;
        return &result;
    }

    buffer[0] = '\0';

    DIR *d = opendir(path);
    if (!d) {             // director inexistent sau fara permisiuni
        result = NULL;    
        return &result;
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;

        size_t used = strlen(buffer);
        size_t freeb = sizeof(buffer) - used - 2; // 1 pt '\n' + 1 pt '\0'
        if (freeb == 0) break;

        int wrote = snprintf(buffer + used, freeb + 1, "%s\n", dir->d_name);
        if (wrote < 0 || (size_t)wrote > freeb) break;
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
static void free_chunk(chunk *res) {
    if (!res) return;
    if (res->filename) {
        free(res->filename);
        res->filename = NULL;
    }
    if (res->data.data_val) {
        free(res->data.data_val);
        res->data.data_val = NULL;
    }
}
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
    if (make_path(path, sizeof(path), argp->filename) != 0) {
        fprintf(stderr, "retrieve_file_1_svc: Failed to construct path for %s\n", argp->filename);
        if(result.filename) {
            free(result.filename);
        }
        if(result.data.data_val) {
            free(result.data.data_val);
        }
        result.filename = strdup(argp->filename);
        result.data.data_len = 0;
        result.data.data_val = NULL;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "retrieve_file_1_svc: Failed to open file %s\n", path);
        if(result.filename) {
            free(result.filename);
        }
        if(result.data.data_val) {
            free(result.data.data_val);
        }
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
        if(result.filename) {
            free(result.filename);
        }
        if(result.data.data_val) {
            free(result.data.data_val);
        }
        result.filename = strdup(argp->filename);
        result.data.data_len = 0;
        result.data.data_val = NULL;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    if(result.data.data_val) {
        free(result.data.data_val);
        result.data.data_val = NULL;
    }
    result.data.data_val = malloc(argp->size);
    if (!result.data.data_val) {
        fprintf(stderr, "retrieve_file_1_svc: Memory allocation failed\n");
        fclose(file);
        if(result.filename) {
            free(result.filename);
        }
        result.filename = strdup(argp->filename);
        result.data.data_len = 0;
        result.size = 0;
        result.dest_offset = 0;
        return &result;
    }

    size_t read_bytes = fread(result.data.data_val, 1, argp->size, file);
    fclose(file);

    if(result.filename) {
        free(result.filename);
    }
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

    if (make_path(path, sizeof(path), argp->filename) != 0) {
        fprintf(stderr, "send_file_1_svc: Failed to construct path for %s\n", argp->filename);
        result = -1;
        return &result;
    }

    FILE *file = fopen(path, "r+b");
    if (!file) {
        file = fopen(path, "w+b");  // daca nu extsta, il cream
    }

    if (file) {
        if (fseek(file, argp->dest_offset, SEEK_SET) != 0) {
            perror("send_file_1_svc fseek");
            fclose(file);
            result = -1;
            return &result;
        }

        size_t written = fwrite(argp->data.data_val, 1, argp->data.data_len, file);
        fclose(file);

        if (written == argp->data.data_len) {
            printf("send_file_1_svc: wrote %zu bytes to %s at offset %d\n", written, path, argp->dest_offset);
            result = 0;
        } else {
            fprintf(stderr, "send_file_1_svc: partial write (%zu/%u) to %s\n", written, argp->data.data_len, path);
            result = -1;
        }
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

// pt citirea din fisier
chunk *mynfs_read_1_svc(request *argp, struct svc_req *req) {
    static chunk result;
    if (result.filename) {
        free(result.filename);
        result.filename = NULL;
    }
    if (result.data.data_val) {
        free(result.data.data_val);
        result.data.data_val = NULL;
    }
    result.data.data_len = 0;
    result.size = 0;
    result.dest_offset = 0;

    if (argp == NULL || argp->filename == NULL) {
        fprintf(stderr, "mynfs_read_1_svc: received NULL request or filename\n");
        result.filename = NULL;
        return &result;
    }

    char path[PATH_MAX];
    if (make_path(path, sizeof(path), argp->filename) != 0) {
        fprintf(stderr, "mynfs_read_1_svc: Failed to construct path for %s\n", argp->filename);
        result.filename = strdup(argp->filename);
        return &result;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "mynfs_read_1_svc: Failed to open file %s\n", path);
        result.filename = strdup(argp->filename);
        return &result;
    }

    if (fseek(file, argp->src_offset, SEEK_SET) != 0) {
        fprintf(stderr, "mynfs_read_1_svc: Failed to seek in file %s\n", path);
        fclose(file);
        result.filename = strdup(argp->filename);
        return &result;
    }

    result.data.data_val = malloc(argp->size);
    if (!result.data.data_val) {
        fprintf(stderr, "mynfs_read_1_svc: Memory allocation failed\n");
        fclose(file);
        result.filename = strdup(argp->filename);
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

// legatura write_1_svc cu send_file_1_svc
int *mynfs_write_1_svc(chunk *argp, struct svc_req *req) {
    return send_file_1_svc(argp, req);  
}


// remdir_1_svc (sterge director recursiv)
static int recursive_remove(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        return -1;
    }
    if (S_ISDIR(statbuf.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return -1;
        struct dirent *entry;
        int ret = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char child_path[PATH_MAX];
            snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
            if (recursive_remove(child_path) != 0) {
                ret = -1;
            }
        }
        closedir(dir);
        if (rmdir(path) != 0) {
            ret = -1;
        }
        return ret;
    } else {
        // nu e folder, sterge fisierul
        return remove(path);
    }
}

// remdir_1_svc
int *mynfs_remdir_1_svc(char **argp, struct svc_req *req) {
    static int result;
    char path[PATH_MAX];

    if (argp == NULL || *argp == NULL || **argp == '\0') {
        fprintf(stderr, "mynfs_remdir_1_svc: received NULL or empty dirname\n");
        result = -1;
        return &result;
    }

    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, *argp);
    if (recursive_remove(path) == 0) {
        printf("mynfs_remdir_1_svc: recursively removed directory %s\n", path);
        result = 0;  // success
    } else {
        perror("mynfs_remdir_1_svc recursive_remove");
        result = -1;  // error
    }
    return &result;
}


// readdir_1_svc
readdir_result *mynfs_readdir_1_svc(readdir_args *argp, struct svc_req *req) {
    static readdir_result result;
    static char name_buf[MAX_FILES][MAX_FILENAME_LENGTH];
    static char *names[MAX_FILES];
    DIR *d;
    struct dirent *dir;
    char path[PATH_MAX];
    int count = 0;

    // resetare rezultat si pointeri
    memset(&result, 0, sizeof(result));
    for (int i = 0; i < MAX_FILES; ++i) {
        names[i] = NULL;
        name_buf[i][0] = '\0';
    }

    if (!argp || !argp->dirname) {
        result.filenames.filenames_val = NULL;
        result.filenames.filenames_len = 0;
        return &result;
    }

    snprintf(path, sizeof(path), "%s/%s", SHARED_DIR, argp->dirname);
    d = opendir(path);
    if (!d) {
        perror("mynfs_readdir_1_svc opendir");
        result.filenames.filenames_val = NULL;
        result.filenames.filenames_len = 0;
        return &result;
    }

    while ((dir = readdir(d)) != NULL && count < MAX_FILES) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;
        // copiere nume fisier in buffer
        strncpy(name_buf[count], dir->d_name, MAX_FILENAME_LENGTH - 1);
        name_buf[count][MAX_FILENAME_LENGTH - 1] = '\0';
        names[count] = name_buf[count];
        count++;
    }
    closedir(d);

    result.filenames.filenames_val = names;
    result.filenames.filenames_len = count;
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
            xdr_free((xdrproc_t)xdr_request, (caddr_t)&req);   // elibereaza argumentele
            xdr_free((xdrproc_t)xdr_chunk, (caddr_t)res);      // elibereaza rezultatul
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
        case MYNFS_MKDIR_PROC: {
            char *arg = NULL;
            if (!svc_getargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg)) {
                svcerr_decode(transp);
                return;
            }
            int *res = mynfs_mkdir_1_svc(&arg, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_int, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg);
            return;
        }
        case MYNFS_REMDIR_PROC: {
            char *arg = NULL;
            if (!svc_getargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg)) {
                svcerr_decode(transp);
                return;
            }
            int *res = mynfs_remdir_1_svc(&arg, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_int, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            svc_freeargs(transp, (xdrproc_t)xdr_wrapstring, (caddr_t)&arg);
            return;
        }
        case MYNFS_READDIR_PROC: {
            readdir_args arg = {0};
            if (!svc_getargs(transp, (xdrproc_t)xdr_readdir_args, (caddr_t)&arg)) {
                svcerr_decode(transp);
                return;
            }
            readdir_result *res = mynfs_readdir_1_svc(&arg, rqstp);
            if (!svc_sendreply(transp, (xdrproc_t)xdr_readdir_result, (caddr_t)res)) {
                svcerr_systemerr(transp);
            }
            xdr_free((xdrproc_t)xdr_readdir_args, (caddr_t)&arg);      
            xdr_free((xdrproc_t)xdr_readdir_result, (caddr_t)res);      
            return;
        }
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
        xdr_free((xdrproc_t)xdr_request, (caddr_t)&req);  
        xdr_free((xdrproc_t)xdr_chunk, (caddr_t)res);     
        return;
    }
        default:
            svcerr_noproc(transp);
            return;
    }
}




// activare server
int main() {

    pmap_unset(NFS_PROGRAM, NFS_VERSION_1);

    // RPC server handle
    SVCXPRT *transp;
    transp = svcudp_create(RPC_ANYSOCK);
    if (transp == NULL) {
        fprintf(stderr, "Error: Unable to create RPC service.\n");
        exit(1);
    }
    printf("RPC service handle created successfully.\n");

    // inregistrare serviciu cu RPC
    if (!svc_register(transp, NFS_PROGRAM, NFS_VERSION_1, nfs_1, IPPROTO_UDP)) {
        fprintf(stderr, "Unable to register (NFS_PROGRAM, NFS_VERSION_1, IPPROTO_UDP).\n");
        exit(1);
    }
    printf("Service registered successfully with program number %d and version %d.\n", NFS_PROGRAM, NFS_VERSION_1);

    // pornire
    printf("Starting svc_run...\n");
    svc_run();  // server loop

    // caz de eroare
    fprintf(stderr, "Error: svc_run returned\n");
    exit(1);
}
