#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <bleed/syscalls.h>
#include <bpm.h>

#define MAX_MOUNTS  32

static void drive_from(drive_t *d, const user_mount_info_t *m) {
    snprintf(d->root, sizeof(d->root), "%s", m->path);
    snprintf(d->fs, sizeof(d->fs), "%s", m->fs);
    d->free = m->blocks_free * m->block_size;
}

// where packages can be, / first (the default, /opt) then each drive someone used -r for
int roots_list(char roots[][BPM_ROOT_MAX], int max) {
    int n = 0;
    snprintf(roots[n++], BPM_ROOT_MAX, "/");
    user_mount_info_t m;
    for (int i = 0; i < MAX_MOUNTS && n < max && _mountinfo((uint64_t)i, &m) == 0; i++) {
        if (!strncmp(m.path, "/mnt/", 5))
            snprintf(roots[n++], BPM_ROOT_MAX, "%s", m.path);
    }
    return n;
}

// the mount a path is on
int drive_of(const char *root, drive_t *out) {
    size_t best = 0;
    user_mount_info_t m;
    for (int i = 0; i < MAX_MOUNTS && _mountinfo((uint64_t)i, &m) == 0; i++) {
        size_t len = strlen(m.path);
        int covers = !strcmp(m.path, "/") ||
                     (!strncmp(root, m.path, len) && (root[len] == '/' || root[len] == 0));
        if (covers && len >= best) {
            best = len;
            drive_from(out, &m);
        }
    }
    if (!best) {
        errno = ENOENT;
        return -1;
    }
    snprintf(out->root, sizeof(out->root), "%s", root);
    return 0;
}

void package_dir(char out[BPM_PATH_MAX], const char *root, const char *name) {
    snprintf(out, BPM_PATH_MAX, "%s%s" PKG_DIR "/%s", root, root[strlen(root) - 1] == '/' ? "" : "/", name);
}

int installed_read(const char *dir, pkg_t *p) {
    char path[BPM_PATH_MAX];
    snprintf(path, sizeof(path), "%s/" PKG_META "/" PKG_INFO, dir);

    char  *text;
    size_t len;
    if (read_file(path, &text, &len, PKG_INFO_MAX) < 0)
        return -1;
    int r = pkg_parse(p, text, len);
    free(text);
    return r;
}

static int try_root(const char *name, const char *root, char dir[BPM_PATH_MAX], pkg_t *p) {
    package_dir(dir, root, name);
    char meta[BPM_PATH_MAX];
    snprintf(meta, sizeof(meta), "%s/" PKG_META "/" PKG_INFO, dir);
    if (!exists(meta))
        return -1;
    return installed_read(dir, p);
}

// where name is installed, on root or anywhere packages can be when root is NULL
int installed_find(const char *name, const char *root, char dir[BPM_PATH_MAX], pkg_t *p) {
    if (root)
        return try_root(name, root, dir, p);

    char roots[MAX_ROOTS][BPM_ROOT_MAX];
    int n = roots_list(roots, MAX_ROOTS);
    for (int i = 0; i < n; i++) {
        if (try_root(name, roots[i], dir, p) == 0)
            return 0;
    }
    return -1;
}
