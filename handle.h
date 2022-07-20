#ifndef HANDLE_H
#define HANDLE_H

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

#include "server.h"

struct ServerConfig {
    uint32_t ip;
    uint16_t port;
    char *path;
    int server_fd;
};

struct ServerConfig *config;

uint64_t htonll(uint64_t val);
uint64_t ntohll(uint64_t val);

void handleMessage(int socket);



#endif