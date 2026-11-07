#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <bpm.h>

static int looks_like_file(const char *arg) {
    size_t len = strlen(arg);
    return strchr(arg, '/') || (len > 5 && !strcmp(arg + len - 5, ".bpkg"));
}

int cmd_list(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int found = 0;
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
            if (e->d_name[0] == '.')
                continue;
            char  dir[BPM_PATH_MAX];
            pkg_t p;
            if (path_join(dir, sizeof(dir), pkgs, e->d_name) < 0 || installed_read(dir, &p) < 0)
                continue;

            if (!found++)
                printf("%-20s %-16s %10s  %s\n", "NAME", "VERSION", "SIZE", "WHERE");
            char size[16];
            human_size(p.size, size);
            printf("%-20s %-16s %10s  %s\n", p.name, p.version, size, dir);
            pkg_free(&p);
        }
        closedir(d);
    }

    if (!found)
        printf("no packages installed\n");
    return 0;
}

static int print_info(void *arg, pkg_t *p) {
    (void)arg;
    pkg_print(p, NULL);
    return 1;
}

int cmd_info(int argc, char **argv) {
    if (argc < 1) {
        bpm_error("info about what? bpm info <name> or bpm info <file.bpkg>");
        return 2;
    }

    int status = 0;
    for (int i = 0; i < argc; i++) {
        if (i)
            printf("\n");
        if (looks_like_file(argv[i])) {
            bpkg_ops_t ops = { print_info, NULL, NULL, NULL, NULL };
            if (bpkg_read(argv[i], &ops) < 0)
                status = 1;
            continue;
        }

        char  dir[BPM_PATH_MAX];
        pkg_t p;
        if (installed_find(argv[i], NULL, dir, &p) < 0) {
            bpm_error("%s isnt installed", argv[i]);
            status = 1;
            continue;
        }
        pkg_print(&p, dir);
        pkg_free(&p);
    }
    return status;
}

int cmd_files(int argc, char **argv) {
    if (argc != 1) {
        bpm_error("bpm files <name>");
        return 2;
    }

    char  dir[BPM_PATH_MAX];
    pkg_t p;
    if (installed_find(argv[0], NULL, dir, &p) < 0) {
        bpm_error("%s isnt installed", argv[0]);
        return 1;
    }
    pkg_free(&p);

    char path[BPM_PATH_MAX];
    snprintf(path, sizeof(path), "%s/" PKG_META "/" PKG_FILES, dir);
    char  *list;
    size_t len;
    if (read_file(path, &list, &len, 64 * 1024 * 1024) < 0) {
        bpm_error("cant read %s: %s", path, strerror(errno));
        return 1;
    }

    for (char *s = list; *s; ) {
        char *nl = strchr(s, '\n');
        if (nl)
            *nl = 0;
        if (*s)
            printf("%s/%s\n", dir, s);
        if (!nl)
            break;
        s = nl + 1;
    }
    free(list);
    return 0;
}

typedef struct {
    pkg_t    pkg;
    uint64_t files;
    uint64_t bytes;
} verify_t;

static int v_info(void *arg, pkg_t *p) {
    verify_t *v = arg;
    v->pkg = *p;
    v->pkg.raw = NULL;
    return 0;
}

static int v_begin(void *arg, const tar_entry_t *e) {
    verify_t *v = arg;
    if (e->type == TAR_FILE || e->type == TAR_HARDLINK)
        v->files++;
    return e->type == TAR_FILE;
}

static int v_data(void *arg, const uint8_t *buf, size_t len) {
    (void)buf;
    verify_t *v = arg;
    v->bytes += len;
    return 0;
}

int cmd_verify(int argc, char **argv) {
    if (argc < 1) {
        bpm_error("bpm verify <file.bpkg>");
        return 2;
    }

    int status = 0;
    for (int i = 0; i < argc; i++) {
        verify_t v;
        memset(&v, 0, sizeof(v));
        bpkg_ops_t ops = { v_info, v_begin, v_data, NULL, &v };
        if (bpkg_read(argv[i], &ops) < 0) {
            status = 1;
            continue;
        }

        char size[16];
        human_size(v.bytes, size);
        printf("%s: %s %s is fine, %llu files, %s\n", argv[i], v.pkg.name, v.pkg.version,
               (unsigned long long)v.files, size);
        if ((v.pkg.size && v.pkg.size != v.bytes) || (v.pkg.files && v.pkg.files != v.files)) {
            printf("  but its PKGINFO says %llu files and %llu bytes\n",
                   (unsigned long long)v.pkg.files, (unsigned long long)v.pkg.size);
        }
    }
    return status;
}
