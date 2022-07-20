#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "dict.h"
#include "server.h"

void offsetMemCpySrc(uint8_t* pDest, const uint8_t* pSrc, const uint8_t srcBitOffset, const size_t size)
{
    if (srcBitOffset == 0)
    {
        for (size_t i = 0; i < size; ++i)
        {
            pDest[i] = pSrc[i];
        }
    }
    else if (size > 0)
    {
        uint8_t v0 = pSrc[0];
        for (size_t i = 0; i < size; ++i)
        {
            uint8_t v1 = pSrc[i + 1];
            pDest[i] = (v0 << srcBitOffset) | (v1 >> (8 - srcBitOffset));
            v0 = v1;            
        }
    }
}

void offsetMemCpyDest(uint8_t* pDest, const uint8_t* pSrc, const uint8_t destBitOffset, const size_t size)
{
    if (destBitOffset == 0)
    {
        for (size_t i = 0; i < size; ++i)
        {
            pDest[i] = pSrc[i];
        }
    }
    else if (size > 0)
    {
        uint8_t mask = (1<<(8-destBitOffset))-1;
        for (size_t i = 0; i < size; ++i)
        {
            pDest[i] = (pDest[i] & ~mask) | ((pSrc[i] >> destBitOffset) & mask);
            pDest[i+1] = (pSrc[i] << (8 - destBitOffset)) & ~mask;            
        }
    }
}


void readData(unsigned char* input, int bitoffset, int bitlen, unsigned char* output) {
    int srcByteOffset = bitoffset / 8;
    uint8_t srcBitOffset = bitoffset % 8;
    int byteLen = bitlen / 8;
    if (bitlen % 8 >0) {
        byteLen++;
    }

    offsetMemCpySrc(output, input + srcByteOffset, srcBitOffset, byteLen);
}

DictNode* newNode() {
    DictNode* node = malloc(sizeof(DictNode));
    node->l = NULL;
    node->r = NULL;
    node->haveData = false;
    return node;
}

void buildDictTree(DictNode* n, unsigned char* data, uint8_t bitlen, uint16_t code) {
    DictNode* node = n;
    for (int i=0; i<bitlen; i++) {
        int skipByte = i / 8;
        int skipOffset = 7 - i % 8;

        uint8_t bit = (data[skipByte] >> skipOffset) & 0x01;

        if (bit == 0x00) {
            if (node->l == NULL) {
                DictNode* newnode = newNode();
                node->l = newnode;
            }
            node = node->l;
        } else {
            if (node->r == NULL) {
                DictNode* newnode = newNode();
                node->r = newnode;
            }
            node = node->r;
        }
    }

    node->data = code;
    node->haveData = true;
}

Dict* initDict() {
    Dict* dict = malloc(sizeof(Dict));
    dict->decodeRoot = newNode();

    FILE* fp;
    fp = fopen("compression.dict", "rb");
    if (fp == NULL) {
        perror("cannot open dict");
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    uint64_t fsize = ftell(fp);

    unsigned char* buffer = malloc(sizeof(unsigned char) * (fsize+1));
    
    rewind(fp);
    fread(buffer, fsize, 1, fp);
    fclose(fp);

    int bitoffset = 0;
    unsigned char bitlen = 0x00;
    for (int i=0; i<256; i++) {
        readData(buffer, bitoffset, 8, &bitlen);
        bitoffset += 8;
        unsigned char* nodedata = malloc(sizeof(unsigned char)*(bitlen/8+1));
        readData(buffer, bitoffset, bitlen, nodedata);
        bitoffset += bitlen;
        dict->encode[i] = nodedata;
        dict->encodeLen[i] = bitlen;
        buildDictTree(dict->decodeRoot, nodedata, bitlen, i);
    }

    free(buffer);
    return dict;
}

void printDict(Dict* dict) {
    for (int i=0; i<256; i++) {
        printf("%x -> length: %d \n", i, dict->encodeLen[i]);
        int bitlen = dict->encodeLen[i];
        int bytelen = bitlen / 8+1;
        for (int j=0; j<bytelen; j++) {
            printf("%x ", dict->encode[i][j]);
        }
        printf("\n");
    }
}

void freeDictNode(DictNode* node) {
    if (node == NULL) return;
    freeDictNode(node->l);
    freeDictNode(node->r);
    free(node);
}

void freeDict(Dict* dict) {
    for (int i=0; i< 256; i++) {
        free(dict->encode[i]);
    }
    freeDictNode(dict->decodeRoot);
    free(dict);
}

int compressData(Dict* dict, const unsigned char* data, int len, unsigned char** output) {
    int l = len > BUFFER_LEN ? len : BUFFER_LEN;
    unsigned char* buffer = malloc(sizeof(unsigned char) * l);

    int bitOffset = 0;
    for (int i=0; i<len; i++) {
        unsigned char* encode = dict->encode[data[i]];
        int bitlen = dict->encodeLen[data[i]];
        int byteOffset = bitOffset / 8;
        int byteLen = bitlen / 8;
        if (bitlen % 8 != 0) {
            byteLen++;
        }

        offsetMemCpyDest(buffer+byteOffset, encode, bitOffset % 8, byteLen);

        bitOffset+=bitlen;
    }

    int byteLen = bitOffset / 8;
    uint8_t padding = 0;
    if (bitOffset % 8 == 0) {
        buffer[byteLen] = 0x00;
    } else {
        byteLen++;
        padding = 8 - bitOffset % 8;
        uint8_t mask = ~((1<<padding)-1);
        buffer[byteLen - 1] &= mask;
        buffer[byteLen] = padding;
    }

    *output = buffer;
    return byteLen+1;
}

int depressData(Dict* dict, const unsigned char* data, int len, unsigned char** output) {
    int l = len > BUFFER_LEN ? len : BUFFER_LEN;
    unsigned char* buffer = malloc(sizeof(unsigned char) * l);
    
    uint8_t padding = data[len - 1];
    int bitLen = 8 * (len-1) - padding;
    int outputByteLen = 0;
    DictNode* node = dict->decodeRoot;

    for (int i=0; i<bitLen; i++) {
        int byteOffset = i / 8;
        uint8_t bitmove = 7 - i%8;
        uint8_t bit = (data[byteOffset] >> bitmove) & 0x01;

        if (bit == 0) {
            node = node->l;
        } else {
            node = node->r;
        }

        if (node->haveData) {
            buffer[outputByteLen] = node->data;
            outputByteLen++;
            node = dict->decodeRoot;
        }
    }

    assert(node == dict->decodeRoot);
    *output = buffer;
    return outputByteLen;
}