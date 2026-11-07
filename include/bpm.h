#pragma once

#include <stddef.h>
#include <stdint.h>
#include <untar.h>

#define BPM_VERSION     "1.0.0"
#define PKG_DIR         "opt"

#ifndef LINK_DIR
#define LINK_DIR        "/usr/local/bin"
#endif
#define PKG_META        ".bpkg"
#define PKG_INFO        "PKGINFO"
#define PKG_FILES       "files"
#define PKG_PAYLOAD     "files/"

#define PKG_NAME_MAX    64
#define PKG_INFO_MAX    (16 * 1024)
#define BPM_PATH_MAX    512

typedef struct {
    char     name[PKG_NAME_MAX];
    char     version[64];
    char     description[256];
    char     exec[256];                 // the programs in it, space separated, empty if none
    uint64_t size;                      // bytes once installed
    uint64_t files;
    char    *raw;                       // PKGINFO as it came, written back out on install
    size_t   raw_len;
} pkg_t;

typedef struct {
    char     root[256];                 // top of the drive, /mnt/nvme
    char     fs[16];
    uint64_t free;                      // bytes
} drive_t;

// pkg.c
int  pkg_parse(pkg_t *p, const char *text, size_t len);
void pkg_free(pkg_t *p);
int  pkg_name_ok(const char *name);
void pkg_print(const pkg_t *p, const char *where);

typedef struct {
    int  (*info)(void *arg, pkg_t *p);
    int  (*begin)(void *arg, const tar_entry_t *e);
    int  (*data)(void *arg, const uint8_t *buf, size_t len);
    int  (*end)(void *arg, const tar_entry_t *e);
    void  *arg;
} bpkg_ops_t;

int bpkg_read(const char *path, const bpkg_ops_t *ops);

// drives.c
#define BPM_ROOT_MAX    256
#define MAX_ROOTS       33          // / and every mount
int  roots_list(char roots[][BPM_ROOT_MAX], int max);
int  drive_of(const char *root, drive_t *out);
int  installed_find(const char *name, const char *root, char dir[BPM_PATH_MAX], pkg_t *p);
int  installed_read(const char *dir, pkg_t *p);
void package_dir(char out[BPM_PATH_MAX], const char *root, const char *name);

// links.c
int  links_add(const pkg_t *p, const char *dir);
void links_remove(const char *dir);
int  cmd_link(int argc, char **argv);

// files.c
int  path_join(char *out, size_t size, const char *a, const char *b);
int  mkdir_p(const char *path);
int  remove_tree(const char *path);
int  read_file(const char *path, char **buf, size_t *len, size_t max);
int  write_file(const char *path, const void *buf, size_t len);
int  copy_file(const char *from, const char *to);
int  is_dir(const char *path);
int  exists(const char *path);
void human_size(uint64_t bytes, char out[16]);

// commands
int cmd_install(int argc, char **argv);
int cmd_remove(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_info(int argc, char **argv);
int cmd_files(int argc, char **argv);
int cmd_verify(int argc, char **argv);

void bpm_error(const char *fmt, ...);
extern int bpm_line_open;
