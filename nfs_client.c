#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <limits.h>
#include "nfs.h"

#define COLOR_RESET   "\x1b[0m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_VIOLET  "\x1b[35m"


#ifndef SERVER_IP
#define SERVER_IP "192.168.64.3"
#endif


static char current_dir[PATH_MAX] = ".";

static const char *commands[] = {
    "list", "make", "remove", "download", "upload",
    "makedr", "remdr", "read", "edit", "chdir",
    "wherepd", "clear", "help", "bye", NULL
};

void suggest_commands(const char *prefix) {
    printf(COLOR_YELLOW "Did you mean:\n" COLOR_RESET);
    for (int i = 0; commands[i]; i++) {
        if (strncmp(commands[i], prefix, strlen(prefix)) == 0) {
            printf("  %s\n", commands[i]);
        }
    }
}
void print_help() {
    printf("\nAvailable commands:\n");
    printf("  list              - list files in current directory\n");
    printf("  make <file>       - create a file\n");
    printf("  remove <file>     - delete a file\n");
    printf("  download <r> <l>  - download remote file to local\n");
    printf("  upload <l> <r>    - upload local file to remote\n");
    printf("  makedr <folder>   - create directory\n");
    printf("  remdr <folder>    - remove directory recursively\n");
    printf("  read <file>       - display file contents\n");
    printf("  edit <file>       - edit file interactively\n");
    printf("  chdir <folder>    - change directory\n");
    printf("  wherepd           - print current directory\n");
    printf("  clear             - clear the screen\n");
    printf("  help              - show this help\n");
    printf("  bye               - exit the client\n\n");
}



/* wrapper pt ls_1 */
char **safe_ls(CLIENT *clnt) {
    char *arg = current_dir;         
    char **res = ls_1(&arg, clnt);    
    if (!res) return NULL;
    // copie locala pt parsing
    static char *copy[MAX_FILES + 1] = {NULL};
    for (int i = 0; i < MAX_FILES; i++) {
        if (copy[i]) {
            free(copy[i]);
            copy[i] = NULL;
        }
    }
    for (int i = 0; i < MAX_FILES; i++) { copy[i] = NULL; }
    if (*res) {
        copy[0] = strdup(*res);       // rpcgen intoarce char* intr-un char**
    }
    copy[MAX_FILES] = NULL;
    xdr_free((xdrproc_t)xdr_wrapstring, (char*)res);

    return copy;
}

/* wrapper pt create_1 */
int safe_create(CLIENT *clnt, const char *filename) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, filename);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }
    char *arg = path;
    int *res = create_1(&arg, clnt);
    if (!res) {
        clnt_perror(clnt, "create_1 failed");
        return -1;
    }
    return *res;
}

/* wrapper pt delete_1 */
int safe_delete(CLIENT *clnt, const char *filename) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, filename);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }
    char *arg = path;
    int *res = delete_1(&arg, clnt);
    if (!res) {
        clnt_perror(clnt, "delete_1 failed");
        return -1;
    }
    return *res;
}

/* wrapper pt retrieve_1 */
int safe_retrieve(CLIENT *clnt, const char *remote_file, const char *local_file) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, remote_file);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }

    request req;
    req.filename = path;
    req.size = 512;           // cat citeste per apel
    req.src_offset = 0;
    req.dest_offset = 0;

    FILE *out = fopen(local_file, "wb");
    if (!out) {
        perror("safe_retrieve fopen");
        return -1;
    }

    while (1) {
        chunk *res = retrieve_file_1(&req, clnt);
        if (!res) {
            break;
        }
        if (res->data.data_len == 0 || res->size < 0) {
            xdr_free((xdrproc_t)xdr_chunk, (char *)res);
            break;
        }

        fwrite(res->data.data_val, 1, res->data.data_len, out);

        // pregatire chunk urmator
        req.src_offset += res->data.data_len;
        req.dest_offset += res->data.data_len;

        // eliberare cu XDR
        xdr_free((xdrproc_t)xdr_chunk, (char *)res);
    }

    fclose(out);
    return 0;
}

/* wrapper pt send_file_1 */
int safe_send(CLIENT *clnt, const char *local_file, const char *remote_file) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, remote_file);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }

    FILE *in = fopen(local_file, "rb");
    if (!in) {
        perror("safe_send fopen");
        return -1;
    }

    chunk ch;
    ch.filename = path;
    ch.dest_offset = 0;

    int res_status = 0;

    while (1) {
        char buffer[512];
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), in);
        if (bytes_read == 0) break;

        ch.data.data_val = malloc(bytes_read);
        if (!ch.data.data_val) {
            fprintf(stderr, "Memory allocation failed\n");
            res_status = -1;
            break;
        }
        memcpy(ch.data.data_val, buffer, bytes_read);
        ch.data.data_len = bytes_read;
        ch.size = bytes_read;

        int *res = send_file_1(&ch, clnt);
        free(ch.data.data_val);
        if (!res || *res != 0) {
            clnt_perror(clnt, "send_file_1 failed");
            res_status = -1;
            break;
        }

        ch.dest_offset += bytes_read;
    }

    fclose(in);
    return res_status;
}

/* wrapper pt mkdir */
int safe_mkdir(CLIENT *clnt, const char *dirname) {
    if (dirname == NULL || strlen(dirname) == 0) {
        fprintf(stderr, "safe_mkdir: invalid dirname\n");
        return -1;
    }

    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, dirname);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }
    char *arg = path;
    int *res = mynfs_mkdir_1(&arg, clnt);

    if (!res) {
        clnt_perror(clnt, "mynfs_mkdir_1 failed");
        return -1;
    }
    int ret = *res;
    xdr_free((xdrproc_t)xdr_int, (char*)res);
    return ret;
}


/* wrapper pt remdir */
int safe_remdir(CLIENT *clnt, const char *dirname) {
    if (dirname == NULL || strlen(dirname) == 0) {
        fprintf(stderr, "safe_remdir: invalid dirname\n");
        return -1;
    }
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, dirname);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }
    char *arg = path;
    int *res = mynfs_remdir_1(&arg, clnt);
    if (!res) {
        clnt_perror(clnt, "mynfs_remdir_1 failed");
        return -1;
    }
    int ret = *res;
    xdr_free((xdrproc_t)xdr_int, (char*)res);
    return ret;
}


/* wrapper pt read */
int safe_read(CLIENT *clnt, const char *filename) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, filename);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }

    request req;
    memset(&req, 0, sizeof(req));
    req.filename = path;
    req.size = 1024; 
    req.src_offset = 0;
    req.dest_offset = 0;

    while (1) {
        chunk *res = mynfs_read_1(&req, clnt);
        if (!res) {
            break;
        }
        if (res->data.data_len <= 0) {
            xdr_free((xdrproc_t)xdr_chunk, (char *)res);
            break;
        }

        fwrite(res->data.data_val, 1, res->data.data_len, stdout);
        fflush(stdout);

        // avansare offset
        req.src_offset += res->data.data_len;
        req.dest_offset += res->data.data_len;

        xdr_free((xdrproc_t)xdr_chunk, (char *)res);

        // eof
        if (res->data.data_len < req.size) {
            break;
        }
    }
    printf("\n");
    return 0;
}

/* wrapper pt edit (nano-like) */
int safe_edit(CLIENT *clnt, const char *filename) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s", current_dir, filename);
    if (written < 0 || written >= (int)sizeof(path)) {
        fprintf(stderr, COLOR_RED "Error: path too long (truncated)\n" COLOR_RESET);
        return -1;
    }

    // afiseaza continutul curent al fisierului
    printf(COLOR_YELLOW "--- Current content of %s ---\n" COLOR_RESET, filename);
    request req;
    memset(&req, 0, sizeof(req));
    req.filename = path;
    req.size = 512;
    req.src_offset = 0;
    req.dest_offset = 0;

    while (1) {
        chunk *res = mynfs_read_1(&req, clnt);
        if (!res) {
            break;
        }
        if (res->data.data_len <= 0) {
            xdr_free((xdrproc_t)xdr_chunk, (char *)res);
            break;
        }
        fwrite(res->data.data_val, 1, res->data.data_len, stdout);
        req.src_offset += res->data.data_len;
        xdr_free((xdrproc_t)xdr_chunk, (char *)res);
    }
    printf("\n" COLOR_YELLOW "--- Enter new content (end with CTRL+D) ---\n" COLOR_RESET);

    char buffer[4096];
    size_t total = 0;
    int c;
    while ((c = getchar()) != EOF) {
        if (total < sizeof(buffer)) {
            buffer[total++] = (char)c;
        } else {
            printf("Buffer full, truncating input.\n");

            while ((c = getchar()) != EOF) { }
            break;
        }
    }
    clearerr(stdin);

    if (total == 0) {
        printf("\nNo new content provided.\n");
        return -1;
    }

    chunk ch;
    memset(&ch, 0, sizeof(ch));
    ch.filename = path;
    ch.data.data_val = malloc(total);
    if (!ch.data.data_val) {
        fprintf(stderr, "\nMemory allocation failed\n");
        return -1;
    }
    memcpy(ch.data.data_val, buffer, total);
    ch.data.data_len = total;
    ch.size = total;
    ch.dest_offset = 0;

    int *res = mynfs_write_1(&ch, clnt);
    free(ch.data.data_val);
    if (!res || *res != 0) {
        fprintf(stderr, "\nFailed to write file %s\n", filename);
        return -1;
    }
    printf(COLOR_GREEN "\nFile %s written successfully.\n" COLOR_RESET, filename);
    return 0;
}

/* wrapper pt chdir */
int safe_chdir(CLIENT *clnt, const char *dirname) {
    if (!dirname || !*dirname) return -1;

    // .. 
    if (strcmp(dirname, "..") == 0) {
        char tmp[PATH_MAX];
        strncpy(tmp, current_dir, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';

        char *slash = strrchr(tmp, '/');
        if (!slash) {                 
            strcpy(current_dir, ".");
            return 0;
        }
        // taie ultimul segment
        *slash = '\0';
        if (tmp[0] == '\0' || strcmp(tmp, ".") == 0)
            strcpy(tmp, ".");
        strncpy(current_dir, tmp, sizeof(current_dir)-1);
        current_dir[sizeof(current_dir)-1] = '\0';
        return 0;
    }

    // compunere
    char candidate[PATH_MAX];
    if (strcmp(current_dir, ".") == 0) {
        int written = snprintf(candidate, sizeof(candidate), "%s", dirname);
        if (written < 0 || written >= (int)sizeof(candidate)) {
            fprintf(stderr, COLOR_RED "\nError: path too long (truncated)\n" COLOR_RESET);
            return -1;
        }
    } else {
        int written = snprintf(candidate, sizeof(candidate), "%s/%s", current_dir, dirname);
        if (written < 0 || written >= (int)sizeof(candidate)) {
            fprintf(stderr, COLOR_RED "\nError: path too long (truncated)\n" COLOR_RESET);
            return -1;
        }
    }

    // validare pe server
    char *arg = candidate;
    char **res = ls_1(&arg, clnt);
    if (!res) return -1;  

    xdr_free((xdrproc_t)xdr_wrapstring, (char*)res);

    strncpy(current_dir, candidate, sizeof(current_dir)-1);
    current_dir[sizeof(current_dir)-1] = '\0';
    return 0;
}








// interactive client for NFS
int main(int argc, char *argv[]) {
    const char *server = (argc > 1) ? argv[1] : SERVER_IP;
    CLIENT *clnt = clnt_create((char *)server, NFS_PROGRAM, NFS_VERSION_1, "udp");
    if (clnt == NULL) {
        clnt_pcreateerror(server);
        return 1;
    }

    printf("Connected to server %s\n", server);
    printf("\n" COLOR_VIOLET "+======================================+\n");
    printf("|                 myNFS                |\n");
    printf("+======================================+\n" COLOR_RESET);
    print_help();

    char input[256];
    while (1) {
        printf("myNFS[%s]> ", current_dir);
        if (!fgets(input, sizeof(input), stdin)) {
            printf("Input error\n");
            continue;
        }
        input[strcspn(input, "\n")] = 0;
        // se da skip la empty input
        if (input[0] == '\0') continue;

        char cmd[32], arg1[128], arg2[128];
        int n = sscanf(input, "%31s %127s %127s", cmd, arg1, arg2);
        if (n < 1) continue;

        if (strcmp(cmd, "list") == 0) {
            char **ls_res = safe_ls(clnt);
            if (ls_res == NULL) {
                clnt_perror(clnt, "ls_1 failed");
            } else {
                printf("\n========= CONTENT OF %s =========\n", current_dir);
                if (ls_res[0] == NULL || ls_res[0][0] == '\0') {
                    printf(COLOR_YELLOW "  (no files)\n" COLOR_RESET);
                } else {
                    char *buf = strdup(ls_res[0]);
                    if (!buf) {
                        fprintf(stderr, "Out of memory\n");
                    } else {
                        printf(COLOR_BLUE "+----+-------------------------+\n" COLOR_RESET);
                        printf(COLOR_BLUE "| ID | File Name               |\n" COLOR_RESET);
                        printf(COLOR_BLUE "+----+-------------------------+\n" COLOR_RESET);
                        int idx = 1;
                        char *line = strtok(buf, "\n");
                        while (line) {
                            printf("| %2d | %-23s |\n", idx++, line);
                            line = strtok(NULL, "\n");
                        }
                        printf(COLOR_BLUE "+----+-------------------------+\n" COLOR_RESET);
                        free(buf);
                    }
                }
                printf("\n\n");

                xdr_free((xdrproc_t)xdr_wrapstring, (char*)ls_res);
            }
        } else if (strcmp(cmd, "make") == 0 && n >= 2) {
            int status = safe_create(clnt, arg1);
            if (status == 0) {
                printf(COLOR_GREEN "✓ File %s created successfully\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "✗ Failed to create file %s\n" COLOR_RESET, arg1);
            }
        } else if (strcmp(cmd, "remove") == 0 && n >= 2) {
            int del_status = safe_delete(clnt, arg1);
            if (del_status == 0) {
                printf(COLOR_GREEN "✓ File %s deleted successfully\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "✗ Failed to delete file %s\n" COLOR_RESET, arg1);
            }
        } else if (strcmp(cmd, "download") == 0 && n >= 3) {
            if (safe_retrieve(clnt, arg1, arg2) == 0) {
                printf(COLOR_GREEN "✓ File downloaded successfully as %s\n" COLOR_RESET, arg2);
            } else {
                fprintf(stderr, COLOR_RED "✗ Error downloading file\n" COLOR_RESET);
            }
        } else if (strcmp(cmd, "upload") == 0 && n >= 3) {
            if (safe_send(clnt, arg1, arg2) == 0) {
                printf(COLOR_GREEN "✓ File uploaded successfully as %s\n" COLOR_RESET, arg2);
            } else {
                fprintf(stderr, COLOR_RED "✗ Error uploading file\n" COLOR_RESET);
            }
        } else if (strcmp(cmd, "makedr") == 0 && n >= 2) {
            int status = safe_mkdir(clnt, arg1);
            if (status == 0) {
                printf(COLOR_GREEN "✓ Directory %s created successfully\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "✗ Failed to create directory %s\n" COLOR_RESET, arg1);
            }
        }
        else if (strcmp(cmd, "remdr") == 0 && n >= 2) {
            char confirm[8];
            printf(COLOR_YELLOW "! Are you sure you want to remove '%s' recursively? (yes/no): " COLOR_RESET, arg1);
            if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
                printf(COLOR_YELLOW "! Aborted.\n" COLOR_RESET);
                continue;
            }
            confirm[strcspn(confirm, "\n")] = 0;
            if (strcmp(confirm, "yes") != 0) {
                printf(COLOR_YELLOW "! Aborted.\n" COLOR_RESET);
                continue;
            }
            int status = safe_remdir(clnt, arg1);
            if (status == 0) {
                printf(COLOR_GREEN "✓ Directory %s removed recursively\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "✗ Failed to remove directory %s\n" COLOR_RESET, arg1);
            }
        }
        else if (strcmp(cmd, "read") == 0 && n >= 2) {
            safe_read(clnt, arg1);
        } else if (strcmp(cmd, "edit") == 0 && n >= 2) {
            safe_edit(clnt, arg1);
        }
        else if (strcmp(cmd, "chdir") == 0 && n >= 2) {
            if (safe_chdir(clnt, arg1) == 0) {
                printf(COLOR_GREEN "✓ Changed directory to %s\n" COLOR_RESET, current_dir);
            } else {
                fprintf(stderr, COLOR_RED "✗ Failed to change directory to %s\n" COLOR_RESET, arg1);
            }
        }
        else if (strcmp(cmd, "wherepd") == 0) {
            printf("Current directory: %s\n", current_dir);
        }
        else if (strcmp(cmd, "clear") == 0) {
            printf("\033[H\033[J");
        }
        else if (strcmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcmp(cmd, "bye") == 0) {
            printf("Gotta go, bye!\n");
            break;
        } else {
            printf(COLOR_RED "Unknown command: %s\n" COLOR_RESET, cmd);
            suggest_commands(cmd);
        }
    }

    clnt_destroy(clnt);
    return 0;
}