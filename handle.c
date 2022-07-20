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
#include <dirent.h>

#include "handle.h"
#include "server.h"

int usingSlot[MAX_THREAD_NUM];
int sessionIdSlot[MAX_THREAD_NUM];
char* filenameSlot[MAX_THREAD_NUM];

uint64_t htonll(uint64_t val) {
    return (((uint64_t) htonl(val)) << 32) + htonl(val >> 32);
}
 
uint64_t ntohll(uint64_t val) {
    return (((uint64_t) ntohl(val)) << 32) + ntohl(val >> 32);
}

void initSlot() {
    for (int i=0; i<MAX_THREAD_NUM; i++) {
        usingSlot[i] = 0;
    }
}

void destroySlot() {
    for (int i=0; i<MAX_THREAD_NUM; i++) {
        if (usingSlot[i] > 0) {
            usingSlot[i] = 0;
            sessionIdSlot[i] = 0;
            free(filenameSlot[i]);
        }
    }
}

int enterSlot(uint32_t sessionId, char* filename) {
    if (pthread_mutex_lock(&mutex) != 0) {
        perror("slotlock error!\n");
    }

    int findIdx = -1;
    for (int i=0; i<MAX_THREAD_NUM; i++) {
        if (usingSlot[i] > 0) {
            if (sessionIdSlot[i] == sessionId) {
                if (strcmp(filename, filenameSlot[i]) != 0) {
                    return -1;
                } else {
                    findIdx = i;
                    break;
                }
            }
            if (strcmp(filenameSlot[i], filename) == 0) {
                if (sessionIdSlot[i] != sessionId) {
                    return -1;
                } else {
                    findIdx = i;
                    break;
                }
            }
        }
    }

    int retId = -1;
    if (findIdx != -1) {
        usingSlot[findIdx]++;
        retId = findIdx;
    } else {
        int emptyId = -1;
        for (int i=0; i<MAX_THREAD_NUM; i++) {
            if (usingSlot[i]==0) {
                emptyId = i;
                break;
            }
        }

        if (emptyId == -1) {
            perror("no empty session slot!!!");
        }

        usingSlot[emptyId]++;
        sessionIdSlot[emptyId] = sessionId;
        filenameSlot[emptyId] = malloc(sizeof(unsigned char) * (strlen(filename) + 1));
        strcpy(filenameSlot[emptyId], filename);
        retId = emptyId;
    }

    pthread_mutex_unlock(&mutex);
    return retId;
}

void leaveSlot(int slotId) {
    if (slotId < 0) {
        return;
    }
    pthread_mutex_lock(&mutex);
    if (usingSlot[slotId] > 0) {
        usingSlot[slotId]--;
        if (usingSlot[slotId] == 0) {
            sessionIdSlot[slotId] = 0;
            free(filenameSlot[slotId]); 
        }
    }
    pthread_mutex_unlock(&mutex);
}

void sendMessage(int socket, uint8_t type, uint8_t compress, u_int64_t len, unsigned char* data) {
    if (compress == 0x01 && len > 0) {
        unsigned char* output;
        len = compressData(dict, data, len, &output);
        data = output;
    }
    unsigned char* buffer = malloc(sizeof(unsigned char) * (len + 9));
    uint8_t header = type << 4 | compress << 3;
    buffer[0] = header;
    uint64_t lenN = htonll(len);
    memcpy(buffer+1, &lenN, 8);
    if (len>0) {
        memcpy(buffer+9, data, len);
    }
    send(socket, buffer, len+9, 0);
    if (compress == 0x01 && len>0) {
        free(data);
    }
    free(buffer);
}

void sendError(int socket) {
    sendMessage(socket, 0x0F, 0x00, 0, NULL);
}

bool handleEcho(int socket, uint64_t paylen, unsigned char* data, uint8_t reqcmp) {
    unsigned char* payload = malloc(sizeof(unsigned char) * paylen);
    if (paylen > 0) {
        memcpy(payload, data, paylen);
    }

    sendMessage(socket, 0x01, reqcmp, paylen, payload);
    free(payload);
    return true;
}

bool handleDirectory(int socket, uint64_t paylen, unsigned char* data, uint8_t reqcmp) {
    if (paylen != 0) {
        return false;
    }

    unsigned char sendBuffer[BUFFER_LEN];

    DIR *folder;
    struct dirent *entry;
    int len = 0;
    folder = opendir(config->path);
    if (folder == NULL) {
        printf("cannot open dir");
        return false;
    }

    while ((entry=readdir(folder))) {
        if (entry->d_type == DT_REG) {
            memcpy(sendBuffer+len, entry->d_name, strlen(entry->d_name));
            len += strlen(entry->d_name);
            sendBuffer[len] = 0x00;
            len++;
        }
    }
    
    closedir(folder);

    if (len == 0) {
        sendBuffer[0] = 0x00;
        len++;
    }

    sendMessage(socket, 0x03, reqcmp, len, sendBuffer);
    return true;
}

bool handleFilesize(int socket, uint64_t paylen, unsigned char* data, uint8_t reqcmp) {
    if (paylen == 0) {
        return false;
    }
    if (data[paylen-1] != 0x00) {
        return false;
    }
    char* filename;
    int pathlen = strlen(config->path);
    filename = (char*)malloc(pathlen + 1 + paylen);
    strcpy(filename, config->path);
    filename[pathlen] = '/';
    memcpy(filename+pathlen+1, data, paylen-1);
    filename[pathlen+paylen] = '\0';

    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        free(filename);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    uint64_t sz = ftell(fp);
    fclose(fp);
    // printf("FILENAME: %s\n", filename);
    // printf("FILESIZE: %ld\n", sz);
    uint64_t nsz = htonll(sz);

    unsigned char sendBuffer[BUFFER_LEN];
    memcpy(sendBuffer, &nsz, 8);
    sendMessage(socket, 0x05, reqcmp, 8, sendBuffer);

    free(filename);
    return true;
}

bool handleRetrieveFile(int socket, uint64_t paylen, unsigned char* data, uint8_t reqcmp) {
    if (paylen < 20) {
        return false;
    }
    if (data[paylen - 1] != 0x00) {
        return false;
    }

    uint32_t sessionId;
    memcpy(&sessionId, data, 4);

    uint64_t offset;
    memcpy(&offset, data+4, 8);
    offset = ntohll(offset);

    uint64_t retrieveLen;
    memcpy(&retrieveLen, data+12, 8);
    retrieveLen = ntohll(retrieveLen);

    char* filename;
    int pathlen = strlen(config->path);
    filename = (char*)malloc(pathlen + 1 + paylen - 20);
    strcpy(filename, config->path);
    filename[pathlen] = '/';
    memcpy(filename+pathlen+1, data+20, paylen-21);
    filename[pathlen+paylen-20] = '\0';

    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("NO such file %s\n", filename);
        free(filename);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    uint64_t sz = ftell(fp);

    if (offset >= sz || offset+retrieveLen > sz) {
        fclose(fp);
        free(filename);
        return false;
    }

    int slotId = enterSlot(sessionId, filename);
    if (slotId == -1) {
        fclose(fp);
        free(filename);
        return false;
    }
    
    unsigned char* output = malloc(sizeof(unsigned char) * retrieveLen + 20);
    fseek(fp, offset, SEEK_SET);

    fread(output+20, retrieveLen, 1, fp);
    fclose(fp);

    memcpy(output, &sessionId, 4);
    uint64_t noffset = htonll(offset);
    memcpy(output+4, &noffset, 8);
    uint64_t nretrieveLen = htonll(retrieveLen);
    memcpy(output+12, &nretrieveLen, 8);

    sendMessage(socket, 0x07, reqcmp, retrieveLen+20, output);

    leaveSlot(slotId);
    free(output);
    free(filename);
    return true;
}

bool handleShutdown() {
    shutdown(config->server_fd, SHUT_RDWR);
    return false;
}

void handleMessage(int socket) {
    unsigned char buffer[BUFFER_LEN];
    int len;
    while ((len = read(socket, buffer, BUFFER_LEN)) > 0) {
        if (len < 9) {
            sendError(socket);
            break;
        }
        uint64_t paylenN;
        memcpy(&paylenN, buffer + 1, sizeof(paylenN));
        uint64_t paylen = ntohll(paylenN);

        uint8_t header = buffer[0];
        uint8_t type = (0xF0 & header) >> 4;
        uint8_t cmp = (0x08 & header) >> 3;
        uint8_t reqCmp = (0x04 & header) >> 2;
        // printf("header%x\n", header);
        // printf("type %x cmp %x reqCmp %x\n", type, cmp, reqCmp);

        bool normal = true;

        len = len - 9;

        if (paylen != len) {
            sendError(socket);
            break;
        }

        unsigned char* payload = buffer+9;

        if (cmp == 0x01) {
            paylen = depressData(dict, buffer+9, len, &payload);
        }

        bool shutdown = false;
        switch (type) {
        case 0x00: // Echo
            normal = handleEcho(socket, paylen, payload, reqCmp);
            break;
        case 0x02: // Diretiry
            normal = handleDirectory(socket, paylen, payload, reqCmp);
            break;
        case 0x04: // File size
            normal = handleFilesize(socket, paylen, payload, reqCmp);
            break;
        case 0x06: // Retrieve File
            normal = handleRetrieveFile(socket, paylen, payload, reqCmp);
            break;
        case 0x08: // shutdown
            normal = handleShutdown();
            shutdown = true;
            break;
        default:
            normal = false;
        }

        if (cmp == 0x01) {
            free(payload);
        }

        if (!normal) {
            if (!shutdown) {
                sendError(socket);
            }
            break;
        }
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
}