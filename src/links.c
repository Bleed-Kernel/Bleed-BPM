#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <bpm.h>

// 1 if path is a link pointing somewhere under dir
static int points_into(const char *path, const char *dir) {
    char target[BPM_PATH_MAX];
    ssize_t n = readlink(path, target, sizeof(target) - 1);
    if (n < 0)
        return 0;
    target[n] = 0;
    size_t len = strlen(dir);
    return !strncmp(target, dir, len) && target[len] == '/';
}

int links_add(const pkg_t *p, const char *dir) {
    if (mkdir_p(LINK_DIR) < 0) {
        bpm_error("cant make %s: %s", LINK_DIR, strerror(errno));
        return -1;
    }

    int status = 0;
    char list[sizeof(p->exec)];
    strcpy(list, p->exec);
    for (char *save, *name = strtok_r(list, " \t", &save); name; name = strtok_r(NULL, " \t", &save)) {
        char link[BPM_PATH_MAX], target[BPM_PATH_MAX];
        if (strchr(name, '/') || path_join(link, sizeof(link), LINK_DIR, name) < 0 ||
            path_join(target, sizeof(target), dir, name) < 0) {
            bpm_error("%s isnt a usable program name", name);
            status = -1;
            continue;
        }
        if (!exists(target)) {
            bpm_error("%s says it has %s but its not there", p->name, name);
            status = -1;
            continue;
        }

        struct stat st;
        if (lstat(link, &st) == 0) {
            // something else of the same name was there first, leave it be
            if (!S_ISLNK(st.st_mode)) {
                bpm_error("%s is already there and isnt a link, %s %s wont be found by name", link, p->name, name);
                status = -1;
                continue;
            }
            unlink(link);
        }
        if (symlink(target, link) < 0) {
            bpm_error("cant link %s to %s: %s", link, target, strerror(errno));
            status = -1;
        }
    }
    return status;
}

// every link in LINK_DIR into dir goes
void links_remove(const char *dir) {
    DIR *d = opendir(LINK_DIR);
    if (!d)
        return;

    // readdir shifts as things go, so names first
    int n = 0, cap = 16;
    char **names = malloc((size_t)cap * sizeof(char *));
    struct dirent *e;
    while (names && (e = readdir(d)) != NULL) {
        char path[BPM_PATH_MAX];
        if (e->d_name[0] == '.' || path_join(path, sizeof(path), LINK_DIR, e->d_name) < 0 || !points_into(path, dir))
            continue;
        if (n == cap) {
            char **grown = realloc(names, (size_t)(cap *= 2) * sizeof(char *));
            if (!grown)
                break;
            names = grown;
        }
        names[n++] = strdup(path);
    }
    closedir(d);

    for (int i = 0; i < n; i++) {
        if (names[i])
            unlink(names[i]);
        free(names[i]);
    }
    free(names);
}

// links for every package in /opt and on every drive, the kernel does the drives when they come in
int cmd_link(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int count = 0, status = 0;
    char roots[MAX_ROOTS][BPM_ROOT_MAX];
    int nroots = roots_list(roots, MAX_ROOTS);
    for (int i = 0; i < nroots; i++) {
        char pkgs[BPM_PATH_MAX];
        if (path_join(pkgs, sizeof(pkgs), roots[i], PKG_DIR) < 0)
            continue;
        DIR *d = opendir(pkgs);
        if (!d)
            continue;

        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char  dir[BPM_PATH_MAX];
            pkg_t p;
            if (e->d_name[0] == '.' || path_join(dir, sizeof(dir), pkgs, e->d_name) < 0 || installed_read(dir, &p) < 0)
                continue;
            if (p.exec[0]) {
                if (links_add(&p, dir) < 0)
                    status = 1;
                printf("%-20s %s\n", p.name, p.exec);
                count++;
            }
            pkg_free(&p);
        }
        closedir(d);
    }
    if (!count)
        printf("no installed packages have programs\n");
    return status;
}
