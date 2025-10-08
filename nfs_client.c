#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
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

#define MAX_FILES 10  // maxim 10 fisiere in director

/* wrapper pt ls_1 */
char **safe_ls(CLIENT *clnt) {
    char *arg = "";
    char **res = ls_1(&arg, clnt);
    if (!res) return NULL;

    static char *copy[MAX_FILES + 1] = {NULL};
    for (int i = 0; i < MAX_FILES; i++) {
        copy[i] = NULL;  // initializare
        if (res[i] != NULL)
            copy[i] = strdup(res[i]);
        else
            break;
    }
    copy[MAX_FILES] = NULL;
    return copy;
}

/* wrapper pt create_1 */
int safe_create(CLIENT *clnt, const char *filename) {
    char *arg = (char *)filename;
    int *res = create_1(&arg, clnt);
    if (!res) {
        clnt_perror(clnt, "create_1 failed");
        return -1;
    }
    return *res;
}

/* wrapper pt delete_1 */
int safe_delete(CLIENT *clnt, const char *filename) {
    char *arg = (char *)filename;
    int *res = delete_1(&arg, clnt);
    if (!res) {
        clnt_perror(clnt, "delete_1 failed");
        return -1;
    }
    return *res;
}

/* wrapper pt retrieve_1 */
int safe_retrieve(CLIENT *clnt, const char *remote_file, const char *local_file) {
    request req;
    req.filename = (char *)remote_file;
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
        if (!res || res->data.data_len == 0 || res->size < 0) break;

        fwrite(res->data.data_val, 1, res->data.data_len, out);

        // pregatire chunk urmator
        req.src_offset += res->data.data_len;
        req.dest_offset += res->data.data_len;

        if (res->data.data_val) free(res->data.data_val);
        if (res->filename) free(res->filename);
    }

    fclose(out);
    return 0;
}

/* wrapper pt send_file_1 */
int safe_send(CLIENT *clnt, const char *local_file, const char *remote_file) {
    FILE *in = fopen(local_file, "rb");
    if (!in) {
        perror("safe_send fopen");
        return -1;
    }

    chunk ch;
    ch.filename = (char *)remote_file;
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

    char *arg = strdup(dirname);   // copie sigura
    if (!arg) {
        perror("strdup");
        return -1;
    }

    int *res = mynfs_mkdir_1(&arg, clnt);
    free(arg);   // eliberare copie dupa apel

    if (!res) {
        clnt_perror(clnt, "mynfs_mkdir_1 failed");
        return -1;
    }
    return *res;
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
    printf("\nCommands:\n");
    printf("  list\n");
    printf("  make <file>\n");
    printf("  remove <file>\n");
    printf("  download <remote> <local>\n");
    printf("  upload <local> <remote>\n");
    printf("  mkdir <folder>\n");
    printf("  bye\n\n");

    char input[256];
    while (1) {
        printf("myNFS> ");
        if (!fgets(input, sizeof(input), stdin)) {
            printf("Input error\n");
            continue;
        }
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        // Skip empty input
        if (input[0] == '\0') continue;

        char cmd[32], arg1[128], arg2[128];
        int n = sscanf(input, "%31s %127s %127s", cmd, arg1, arg2);
        if (n < 1) continue;

        if (strcmp(cmd, "list") == 0) {
            char **ls_res = safe_ls(clnt);
            if (ls_res == NULL) {
                clnt_perror(clnt, "ls_1 failed");
            } else {
                printf("=== Lista fisiere ===\n");
                if (ls_res[0] == NULL) {
                    printf(COLOR_YELLOW "  (no files)\n" COLOR_RESET);
                } else {
                    printf(COLOR_BLUE "+----+-------------------------+\n" COLOR_RESET);
                    printf(COLOR_BLUE "| ID | File Name               |\n" COLOR_RESET);
                    printf(COLOR_BLUE "+----+-------------------------+\n" COLOR_RESET);
                    char *line = strtok(ls_res[0], "\n");
                    int idx = 1;
                    while (line != NULL) {
                        printf("| %2d | %-23s |\n", idx++, line);
                        line = strtok(NULL, "\n");
                    }
                    printf(COLOR_BLUE "+----+-------------------------+\n" COLOR_RESET);
                }
                printf("\n\n");
                for (int i = 0; i < MAX_FILES && ls_res[i] != NULL; i++) {
                    free(ls_res[i]);
                }
            }
        } else if (strcmp(cmd, "make") == 0 && n >= 2) {
            int status = safe_create(clnt, arg1);
            if (status == 0) {
                printf(COLOR_GREEN "File %s created successfully\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "Failed to create file %s\n" COLOR_RESET, arg1);
            }
        } else if (strcmp(cmd, "remove") == 0 && n >= 2) {
            int del_status = safe_delete(clnt, arg1);
            if (del_status == 0) {
                printf(COLOR_GREEN "File %s deleted successfully\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "Failed to delete file %s\n" COLOR_RESET, arg1);
            }
        } else if (strcmp(cmd, "download") == 0 && n >= 3) {
            if (safe_retrieve(clnt, arg1, arg2) == 0) {
                printf(COLOR_GREEN "File downloaded successfully as %s\n" COLOR_RESET, arg2);
            } else {
                printf(COLOR_RED "Error downloading file\n" COLOR_RESET);
            }
        } else if (strcmp(cmd, "upload") == 0 && n >= 3) {
            if (safe_send(clnt, arg1, arg2) == 0) {
                printf(COLOR_GREEN "File uploaded successfully as %s\n" COLOR_RESET, arg2);
            } else {
                printf(COLOR_RED "Error uploading file\n" COLOR_RESET);
            }
        } else if (strcmp(cmd, "mkdir") == 0 && n >= 2) {
            int status = safe_mkdir(clnt, arg1);
            if (status == 0) {
                printf(COLOR_GREEN "Directory %s created successfully\n" COLOR_RESET, arg1);
            } else {
                fprintf(stderr, COLOR_RED "Failed to create directory %s\n" COLOR_RESET, arg1);
            }
        }
        else if (strcmp(cmd, "bye") == 0) {
            break;
        } else {
            printf("Unknown command.\n");
        }
    }

    clnt_destroy(clnt);
    return 0;
}
