#ifndef SERVER_H
#define SERVER_H

#include "dict.h"

#define MAX_PATH_LENGTH 512
#define MAX_THREAD_NUM 1024
#define BUFFER_LEN 4096

extern Dict* dict;
extern pthread_mutex_t mutex;

#endif