#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

#include "handle.h"
#include "dict.h"
#include "server.h"

pthread_t threads[MAX_THREAD_NUM];
int usingsocket[MAX_THREAD_NUM];
pthread_mutex_t mutex;
Dict* dict;

int initServerConfig(struct ServerConfig *config, char *configFile) {
    FILE *fp;
    fp = fopen(configFile, "rb");
    if (fp == NULL) {
        perror("initServerConfig failed: ");
        return -1;
    }

    fread(&(config->ip), sizeof(config->ip), 1, fp);
    fread(&(config->port), sizeof(config->port), 1, fp);

    int read = fread(config->path, sizeof(unsigned char), MAX_PATH_LENGTH, fp);
    config->path[read] = '\0';

    fclose(fp);
    return 0;
}

int testSaveServerConfig(struct ServerConfig *config, char *configFile) {
    FILE *fp;
    fp = fopen(configFile, "wb");
    if (fp == NULL) {
        perror("initServerConfig failed: ");
        return -1;
    }

    fwrite(&(config->ip), sizeof(config->ip), 1, fp);
    fwrite(&(config->port), sizeof(config->port), 1, fp);
    fwrite(config->path, strlen(config->path), 1, fp);
    fclose(fp);
    return 0;
}

void generateConfigFile() {
    unsigned char bytes[4];
    bytes[0] = 192;
    bytes[1] = 0;
    bytes[2] = 2;
    bytes[3] = 1;
    memcpy(&(config->ip), bytes, 4);
    bytes[0] = 0x16;
    bytes[1] = 0x2e;
    memcpy(&(config->port), bytes, 2);
    config->path=".";
    testSaveServerConfig(config, "test");
}

struct threadArgs {
    int id;
    int socket;
};

void* threadRun(void *arg) {
    struct threadArgs* ta = (struct threadArgs*) arg;

    handleMessage(ta->socket);

    if (pthread_mutex_lock(&mutex) != 0){
        printf("thread lock error!\n");
        return NULL;
    }

    usingsocket[ta->id] = -1;

    pthread_mutex_unlock(&mutex);
    free(ta);
    return NULL;
}

int networklisten(struct ServerConfig *config) {
    initSlot();
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if((server_fd = socket(AF_INET,SOCK_STREAM,0))==0){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if(setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
        perror("set socketopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    // address.sin_addr.s_addr = config->ip;
    address.sin_addr.s_addr = 0;
    address.sin_port = config->port;    
    
    if(bind(server_fd,(struct sockaddr *)&address,sizeof(address))<0){
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if(listen(server_fd, 3)<0){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    config->server_fd = server_fd;
    while (true) {
        if((new_socket = accept(server_fd,(struct sockaddr *)&address,(socklen_t *)&addrlen))<0){
            // perror("accept");
            // exit(EXIT_FAILURE);
            break;
        }

        if (pthread_mutex_lock(&mutex) != 0){
            printf("lock error!\n");
        }

        bool ok = false;
        for (int i=0; i<MAX_THREAD_NUM; i++) {
            if (usingsocket[i] == -1) {
                ok = true;
                usingsocket[i] = new_socket;
                struct threadArgs* ta = malloc(sizeof(struct threadArgs));
                ta->socket = new_socket;
                ta->id = i;
                pthread_create(&threads[i], NULL, threadRun, (void*) ta);
                pthread_detach(threads[i]);
                break;
            }
        }
        if (!ok) {
            perror("no available pthread!!!");
            shutdown(new_socket, SHUT_RDWR);
        }

        pthread_mutex_unlock(&mutex);
    }

    pthread_mutex_lock(&mutex);
    for (int i=0; i<MAX_THREAD_NUM; i++) {
        if (usingsocket[i] != -1) {
            shutdown(usingsocket[i], SHUT_RDWR);
        }
    }
    destroySlot();
    pthread_mutex_unlock(&mutex);

    return 0;
}

int main(int argc, char **argv){
    if (pthread_mutex_init(&mutex, NULL) != 0){
        return -1;
    }

    if (argc != 2) {
        return -1;
    }

    config = malloc(sizeof(struct ServerConfig));
    config->path = malloc(sizeof(unsigned char) * MAX_PATH_LENGTH + 1);


    for (int i=0; i<MAX_PATH_LENGTH; i++) {
        usingsocket[i] = -1;
    }

    initServerConfig(config, argv[1]);
    dict = initDict();

    networklisten(config);

    pthread_mutex_destroy(&mutex);
    freeDict(dict);
    free(config->path);
    free(config);
    return 0;
}