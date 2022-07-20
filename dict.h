#ifndef DICT_H
#define DICT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct dictNode {
    struct dictNode* l;
    struct dictNode* r;
    uint16_t data;
    bool haveData;
} DictNode;

typedef struct dict {
    unsigned char* encode[256];
    int encodeLen[256];
    DictNode* decodeRoot;
} Dict;

Dict* initDict();
void initSlot();
void destroySlot();
void freeDict(Dict* dict);
void printDict(Dict* dict);
int compressData(Dict* dict, const unsigned char* data, int len, unsigned char** output);
int depressData(Dict* dict, const unsigned char* data, int len, unsigned char** output);

#endif