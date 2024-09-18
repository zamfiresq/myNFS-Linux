#include <stdio.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "nfs.h"

// initializare socket 
void setup_socket() {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // struct pentru a seta adresa si portul serverului
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(12345); // port mai mare de 1024
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // bind pentru a lega socketul de adresa si port
    if (bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
}

int main() {
    CLIENT *clnt;
    char *server = "192.168.64.3";  // adresa ip a serverului
    char *progname = "testFisier.txt";  // exemplu de fisier

    // initializare client RPC
    clnt = clnt_create(server, NFS_PROGRAM, NFS_VERSION_1, "udp");

    if (clnt == NULL) {
        clnt_pcreateerror("Error creating RPC client");
        return 1;
    }


    // functiile RPC 
    // listarea fisierelor
    char **files = ls_1(&progname, clnt);
    if (files == NULL) {
        clnt_perror(clnt, "Error calling ls_1");
        clnt_destroy(clnt);
        return 1;
    }

    // afisare
    for (int i = 0; files[i] != NULL; i++) {
        printf("%s\n", files[i]);
    }

    // eliberam memoria alocata 
    for (int i = 0; files[i] != NULL; i++) {
        free(files[i]);
    }
    free(files);

    // functia de creare a unui fisier
    int *result = create_1(&progname, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling create_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Create result: %d\n", *result);

    // functia de stergere a unui fisier
    result = delete_1(&progname, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling delete_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Delete result: %d\n", *result);

    // functia de retrieve file 
    request req;
    req.filename = progname;
    req.start = 0;
    chunk *file = retrieve_file_1(&req, clnt);
    if (file == NULL) {
        clnt_perror(clnt, "Error calling retrieve_file_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Retrieve file result: %s\n", file->data.data_val);

    // trimiterea unui fisier
    result = send_file_1(file, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling send_file_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Send file result: %d\n", *result);

    // crearea unui director
    result = mynfs_mkdir_1(&progname, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling mynfs_mkdir_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Mkdir result: %d\n", *result);

    // deschidere fisier
    result = mynfs_open_1(&progname, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling mynfs_open_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Open result: %d\n", *result);

    // inchidere fisier
    result = mynfs_close_1(&progname, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling mynfs_close_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Close result: %d\n", *result);

    // citire fisier
    file = mynfs_read_1(&req, clnt);
    if (file == NULL) {
        clnt_perror(clnt, "Error calling mynfs_read_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Read result: %s\n", file->data.data_val);

    // scrierea in fisier
    result = mynfs_write_1(file, clnt);
    if (result == NULL) {
        clnt_perror(clnt, "Error calling mynfs_write_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Write result: %d\n", *result);

    // deschidere director
    opendir_args od_args;
    od_args.dirname = progname;
    int *dir_result = mynfs_opendir_1(&od_args, clnt);
    if (dir_result == NULL) {
        clnt_perror(clnt, "Error calling mynfs_opendir_1");
        clnt_destroy(clnt);
        return 1;
    }

    printf("Opendir result: %d\n", *dir_result);

    // citire din director
    readdir_args rd_args;
    rd_args.dirname = progname;
    readdir_result *dir = mynfs_readdir_1(&rd_args, clnt);

    if (dir == NULL) {
        clnt_perror(clnt, "Error calling mynfs_readdir_1");
        clnt_destroy(clnt);
        return 1;
    }

    // afisare fisiere
    for (int i = 0; i < dir->size; i++) {
        printf("Readdir result: %d\n", dir->filenames[i]); 
    }

    // eliberare memorie alocata
    for (int i = 0; i < dir->size; i++) {
        free(dir->filenames[i]);  // eliberam memoria alocata pentru fiecare fisier
    }
    free(dir->filenames);  // eliberare meorie alocata pentru vectorul de fisiere


    // clean up
    clnt_destroy(clnt);
    return 0;
}
