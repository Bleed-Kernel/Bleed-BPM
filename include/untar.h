#pragma once

#include <stddef.h>
#include <stdint.h>

#define TAR_PATH_MAX    256

#define TAR_FILE        '0'
#define TAR_HARDLINK    '1'
#define TAR_SYMLINK     '2'
#define TAR_DIR         '5'

#define TAR_ECORRUPT    -2
#define TAR_ENAME       -3
#define TAR_ENOMEM      -4
#define TAR_ETRUNC      -5

typedef struct {
    char     path[TAR_PATH_MAX];
    char     link[TAR_PATH_MAX];
    char     type; 
    uint64_t size;
} tar_entry_t;

typedef struct {
    int  (*begin)(void *arg, const tar_entry_t *e);
    int  (*data)(void *arg, const uint8_t *buf, size_t len);
    int  (*end)(void *arg, const tar_entry_t *e);
    void  *arg;
} tar_ops_t;

typedef struct tar tar_t;

tar_t *tar_open(const tar_ops_t *ops);
void   tar_close(tar_t *t);

int tar_feed(tar_t *t, const uint8_t *buf, size_t len);
int tar_finish(tar_t *t);

const char *tar_error(int r);
