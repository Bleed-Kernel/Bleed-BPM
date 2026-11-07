#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <bpm.h>

int path_join(char *out, size_t size, const char *a, const char *b) {
    size_t alen = strlen(a);
    int n = (alen && a[alen - 1] == '/') ? snprintf(out, size, "%s%s", a, b)
                                         : snprintf(out, size, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int mkdir_p(const char *path) {
    char buf[BPM_PATH_MAX];
    if (strlen(path) >= sizeof(buf)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(buf, path);

    for (char *p = buf + 1; ; p++) {
        if (*p != '/' && *p)
            continue;
        char c = *p;
        *p = 0;
        if (mkdir(buf, 0755) < 0 && errno != EEXIST && !is_dir(buf))
            return -1;
        *p = c;
        if (!c)
            break;
    }
    return 0;
}

// readdir is index based on bleed and shifts as things go, so names are all read first
static char **list_dir(const char *path, int *count) {
    DIR *d = opendir(path);
    if (!d)
        return NULL;

    int cap = 16, n = 0;
    char **names = malloc((size_t)cap * sizeof(char *));
    struct dirent *e;
    while (names && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (n == cap) {
            cap *= 2;
            char **grown = realloc(names, (size_t)cap * sizeof(char *));
            if (!grown)
                break;
            names = grown;
        }
        names[n] = strdup(e->d_name);
        if (names[n])
            n++;
    }
    closedir(d);
    *count = n;
    return names;
}

int remove_tree(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0)
        return errno == ENOENT ? 0 : -1;

    int status = 0;
    if (S_ISDIR(st.st_mode)) {
        int count = 0;
        char **names = list_dir(path, &count);
        if (!names)
            return -1;

        char child[BPM_PATH_MAX];
        for (int i = 0; i < count; i++) {
            if (path_join(child, sizeof(child), path, names[i]) < 0 || remove_tree(child) < 0)
                status = -1;
            free(names[i]);
        }
        free(names);
        if (status)
            return status;
        return rmdir(path);
    }
    return unlink(path);
}

int read_file(const char *path, char **buf, size_t *len, size_t max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    size_t cap = 4096, n = 0;
    char *b = malloc(cap + 1);
    while (b) {
        if (n == cap) {
            if (cap >= max) {
                free(b);
                close(fd);
                errno = EFBIG;
                return -1;
            }
            cap *= 2;
            char *grown = realloc(b, cap + 1);
            if (!grown)
                break;
            b = grown;
        }
        long r = read(fd, b + n, cap - n);
        if (r < 0) {
            free(b);
            close(fd);
            return -1;
        }
        if (r == 0) {
            close(fd);
            b[n] = 0;
            *buf = b;
            *len = n;
            return 0;
        }
        n += (size_t)r;
    }
    free(b);
    close(fd);
    errno = ENOMEM;
    return -1;
}

static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len) {
        long r = write(fd, p, len);
        if (r <= 0) {
            if (r == 0)
                errno = ENOSPC;
            return -1;
        }
        p   += r;
        len -= (size_t)r;
    }
    return 0;
}

int write_file(const char *path, const void *buf, size_t len) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    int r = write_all(fd, buf, len);
    int err = errno;
    close(fd);
    errno = err;
    return r;
}

int copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY);
    if (in < 0)
        return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }

    static char buf[64 * 1024];
    int status = 0;
    for (;;) {
        long r = read(in, buf, sizeof(buf));
        if (r <= 0) {
            status = r < 0 ? -1 : 0;
            break;
        }
        if (write_all(out, buf, (size_t)r) < 0) {
            status = -1;
            break;
        }
    }
    int err = errno;
    close(in);
    close(out);
    errno = err;
    return status;
}

void human_size(uint64_t bytes, char out[16]) {
    static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    uint64_t scaled = bytes * 10;
    while (scaled >= 10240 && u < 4) {
        scaled /= 1024;
        u++;
    }
    if (u == 0 || scaled >= 1000)
        snprintf(out, 16, "%llu %s", (unsigned long long)(scaled / 10), units[u]);
    else
        snprintf(out, 16, "%llu.%llu %s", (unsigned long long)(scaled / 10), (unsigned long long)(scaled % 10), units[u]);
}
