#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bpm.h>

// a list from somewhere else could try to point outside the package
static int escapes(const char *p) {
    if (p[0] == '/')
        return 1;
    for (const char *s = p; (s = strstr(s, "..")) != NULL; s += 2) {
        int start = s == p || s[-1] == '/';
        int end   = s[2] == 0 || s[2] == '/';
        if (start && end)
            return 1;
    }
    return 0;
}

static int remove_listed(const char *dir, char *list, size_t len, int *kept) {
    // the list is in archive order, folders before whats in them, so backwards empties them first
    int lines = 0;
    for (size_t i = 0; i < len; i++) {
        if (list[i] == '\n') {
            list[i] = 0;
            lines++;
        }
    }

    char **paths = malloc((size_t)(lines + 1) * sizeof(char *));
    if (!paths) {
        bpm_error("out of memory");
        return -1;
    }
    int n = 0;
    for (char *p = list; p < list + len; p += strlen(p) + 1) {
        if (*p)
            paths[n++] = p;
    }

    int failed = 0;
    char path[BPM_PATH_MAX];
    for (int i = n - 1; i >= 0; i--) {
        size_t plen = strlen(paths[i]);
        int folder = paths[i][plen - 1] == '/';
        if (folder)
            paths[i][plen - 1] = 0;
        if (escapes(paths[i]) || path_join(path, sizeof(path), dir, paths[i]) < 0)
            continue;

        if ((folder ? rmdir(path) : unlink(path)) == 0 || errno == ENOENT)
            continue;
        if (folder && errno == ENOTEMPTY) {
            (*kept)++;
            continue;
        }
        bpm_error("cant remove %s: %s", path, strerror(errno));
        failed++;
    }
    free(paths);
    return failed ? -1 : 0;
}

static int remove_one(const char *name, const char *root, int purge) {
    char  dir[BPM_PATH_MAX];
    pkg_t p;
    if (installed_find(name, root, dir, &p) < 0) {
        bpm_error("%s isnt installed%s%s", name, root ? " on " : "", root ? root : "");
        return -1;
    }
    // links point at the absolute path
    char abs[BPM_PATH_MAX];
    if (realpath(dir, abs))
        strcpy(dir, abs);

    char meta[BPM_PATH_MAX], files[BPM_PATH_MAX], info[BPM_PATH_MAX];
    snprintf(meta, sizeof(meta), "%s/" PKG_META, dir);
    snprintf(files, sizeof(files), "%s/" PKG_FILES, meta);
    snprintf(info, sizeof(info), "%s/" PKG_INFO, meta);

    printf("removing %s %s from %s\n", p.name, p.version, dir);
    links_remove(dir);

    int status = 0, kept = 0;
    char  *list;
    size_t len;
    if (purge) {
        status = remove_tree(dir);
    } else if (read_file(files, &list, &len, 64 * 1024 * 1024) < 0) {
        printf("its file list is missing, removing the whole folder\n");
        status = remove_tree(dir);
    } else {
        status = remove_listed(dir, list, len, &kept);
        free(list);

        unlink(files);
        unlink(info);
        rmdir(meta);
        if (rmdir(dir) < 0 && errno == ENOTEMPTY) {
            printf("kept %s, there are files in it bpm didnt install (bpm remove -p %s deletes them too)\n", dir, name);
        } else if (kept) {
            printf("kept %d folders with other files in them\n", kept);
        }
    }

    if (status < 0)
        bpm_error("%s wasnt removed completely", name);
    else
        printf("removed %s %s\n", p.name, p.version);
    pkg_free(&p);
    return status;
}

int cmd_remove(int argc, char **argv) {
    const char *root = NULL;
    int purge = 0, names = 0, status = 0;

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-p")) {
            purge = 1;
        } else if (!strcmp(argv[i], "-r")) {
            if (++i == argc) {
                bpm_error("-r needs a drive, like -r /mnt/usb");
                return 2;
            }
            root = argv[i];
        } else if (argv[i][0] == '-') {
            bpm_error("remove doesnt know %s", argv[i]);
            return 2;
        }
    }

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) {
            i++;
            continue;
        }
        if (argv[i][0] == '-')
            continue;
        names++;
        if (remove_one(argv[i], root, purge) < 0)
            status = 1;
    }

    if (!names) {
        bpm_error("remove what? bpm remove <name>");
        return 2;
    }
    return status;
}
