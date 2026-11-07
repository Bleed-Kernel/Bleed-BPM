#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <bpm.h>

// a package unpacks into /opt/.<name>.part (or <drive>/opt with -r)
typedef struct {
    const char *file;
    const char *root_opt;
    int         force;

    pkg_t       pkg;
    drive_t     drive;
    char        pkgs[BPM_PATH_MAX];     // <drive>/packages
    char        stage[BPM_PATH_MAX];
    char        final[BPM_PATH_MAX];
    char        old[BPM_PATH_MAX];      // install being replaced, empty if none
    int         staged;

    int         fd;
    char        made[BPM_PATH_MAX];     // last folder mkdir_p made, most files share one
    uint64_t    done;
    int         shown;                  // percent last printed

    char       *list;                   // .bpkg/files, one path a line, folders end in /
    size_t      list_len, list_cap;
} install_t;

static int list_add(install_t *in, const char *path, int dir) {
    size_t need = strlen(path) + 2;
    if (in->list_len + need + 1 > in->list_cap) {
        size_t cap = in->list_cap ? in->list_cap * 2 : 4096;
        while (cap < in->list_len + need + 1)
            cap *= 2;
        char *grown = realloc(in->list, cap);
        if (!grown) {
            bpm_error("out of memory");
            return -1;
        }
        in->list = grown;
        in->list_cap = cap;
    }
    in->list_len += (size_t)sprintf(in->list + in->list_len, "%s%s\n", path, dir ? "/" : "");
    return 0;
}

static void progress(install_t *in, int last) {
    if (!in->pkg.size)
        return;
    int pct = (int)(in->done * 100 / in->pkg.size);
    if (pct > 100)
        pct = 100;
    if (pct == in->shown && !last)
        return;
    in->shown = pct;

    char done[16], total[16];
    human_size(in->done, done);
    human_size(in->pkg.size, total);
    printf("\r  %3d%%  %s of %s   ", pct, done, total);
    bpm_line_open = !last;
    if (last)
        printf("\n");
    fflush(stdout);
}

// the top of the drive an installed package lives on, its dir minus /opt/<name>
static void root_of(char out[BPM_PATH_MAX], const char *dir) {
    snprintf(out, BPM_PATH_MAX, "%s", dir);
    for (int cut = 0; cut < 2; cut++) {
        char *slash = strrchr(out, '/');
        if (slash && slash != out)
            *slash = 0;
    }
}

static int pick_drive(install_t *in) {
    uint64_t need = in->pkg.size + in->pkg.size / 20 + 1024 * 1024;

    if (in->root_opt) {
        if (!is_dir(in->root_opt)) {
            bpm_error("%s isnt a folder", in->root_opt);
            return -1;
        }
        // the links in LINK_DIR need absolute targets
        char abs[BPM_PATH_MAX];
        if (!realpath(in->root_opt, abs) || drive_of(abs, &in->drive) < 0) {
            bpm_error("%s isnt on a drive bleed knows", in->root_opt);
            return -1;
        }
    } else if (in->old[0]) {
        // a reinstall goes where the old one was
        char root[BPM_PATH_MAX];
        root_of(root, in->old);
        if (drive_of(root, &in->drive) < 0) {
            bpm_error("cant find the drive %s is on", in->old);
            return -1;
        }
    } else if (drive_of("/", &in->drive) < 0) {
        bpm_error("cant find the root filesystem");
        return -1;
    }

    if (in->drive.free < need && in->pkg.size) {
        char want[16], have[16];
        human_size(need, want);
        human_size(in->drive.free, have);
        bpm_error("%s needs %s but %s only has %s free", in->pkg.name, want, in->drive.root, have);
        if (in->old[0])
            fprintf(stderr, "  bpm remove %s first, the old one is kept until the new one is in\n", in->pkg.name);
        return -1;
    }
    return 0;
}

static int on_info(void *arg, pkg_t *p) {
    install_t *in = arg;
    in->pkg = *p;
    in->pkg.raw = malloc(p->raw_len + 1);
    if (!in->pkg.raw) {
        bpm_error("out of memory");
        return -1;
    }
    memcpy(in->pkg.raw, p->raw, p->raw_len + 1);

    pkg_t old;
    char  dir[BPM_PATH_MAX];
    int found = in->root_opt && installed_find(p->name, in->root_opt, dir, &old) == 0;
    if (!found)
        found = installed_find(p->name, NULL, dir, &old) == 0;
    if (found) {
        int same = !strcmp(old.version, p->version);
        if (!in->force) {
            bpm_error("%s %s is already installed in %s", old.name, old.version, dir);
            fprintf(stderr, "  bpm install -f %s %s it\n", in->file, same ? "reinstalls" : "replaces");
            pkg_free(&old);
            return -1;
        }
        printf("replacing %s %s in %s\n", old.name, old.version, dir);
        strcpy(in->old, dir);
        pkg_free(&old);
    }

    if (pick_drive(in) < 0)
        return -1;

    package_dir(in->final, in->drive.root, p->name);
    snprintf(in->pkgs, sizeof(in->pkgs), "%s%s" PKG_DIR, in->drive.root,
             in->drive.root[strlen(in->drive.root) - 1] == '/' ? "" : "/");
    snprintf(in->stage, sizeof(in->stage), "%s/.%s.part", in->pkgs, p->name);

    if (exists(in->final) && strcmp(in->final, in->old)) {
        bpm_error("%s is already there and bpm didnt put it there, move it out of the way first", in->final);
        return -1;
    }

    if (mkdir_p(in->pkgs) < 0) {
        bpm_error("cant make %s: %s", in->pkgs, strerror(errno));
        return -1;
    }
    // whatever an install that got cut off left behind
    remove_tree(in->stage);
    if (mkdir(in->stage, 0755) < 0) {
        bpm_error("cant make %s: %s", in->stage, strerror(errno));
        return -1;
    }
    in->staged = 1;

    char size[16];
    human_size(p->size, size);
    printf("installing %s %s (%s) into %s\n", p->name, p->version, size, in->final);
    fflush(stdout);
    return 0;
}

static int parent_dir(install_t *in, const char *path) {
    char parent[BPM_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", path);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return 0;
    *slash = 0;
    if (!strcmp(parent, in->made))
        return 0;
    if (mkdir_p(parent) < 0) {
        bpm_error("cant make %s: %s", parent, strerror(errno));
        return -1;
    }
    strcpy(in->made, parent);
    return 0;
}

static int on_begin(void *arg, const tar_entry_t *e) {
    install_t *in = arg;
    char dest[BPM_PATH_MAX];
    if (path_join(dest, sizeof(dest), in->stage, e->path) < 0) {
        bpm_error("path too long: %s", e->path);
        return -1;
    }

    switch (e->type) {
        case TAR_DIR:
            if (mkdir_p(dest) < 0) {
                bpm_error("cant make %s: %s", dest, strerror(errno));
                return -1;
            }
            return list_add(in, e->path, 1);

        case TAR_FILE:
            if (parent_dir(in, dest) < 0)
                return -1;
            in->fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (in->fd < 0) {
                bpm_error("cant make %s: %s", dest, strerror(errno));
                return -1;
            }
            return list_add(in, e->path, 0) < 0 ? -1 : 1;

        case TAR_HARDLINK: {
            // no links on bleed, a copy of the file it points at does the same job
            char from[BPM_PATH_MAX];
            if (parent_dir(in, dest) < 0 || path_join(from, sizeof(from), in->stage, e->link) < 0)
                return -1;
            if (copy_file(from, dest) < 0) {
                bpm_error("cant copy %s to %s: %s", e->link, e->path, strerror(errno));
                return -1;
            }
            return list_add(in, e->path, 0);
        }

        case TAR_SYMLINK:
            bpm_error("skipping the symlink %s -> %s, bleed has no links", e->path, e->link);
            return 0;
    }

    bpm_error("skipping %s, its not a file or a folder", e->path);
    return 0;
}

// THIS IS A BAD DESIGN!!!!!
#define WRITE_MAX   (32 * 1024)

static int on_data(void *arg, const uint8_t *buf, size_t len) {
    install_t *in = arg;
    while (len) {
        long r = write(in->fd, buf, len < WRITE_MAX ? len : WRITE_MAX);
        if (r <= 0) {
            bpm_error("writing into %s failed: %s", in->stage, r < 0 ? strerror(errno) : "the drive is full");
            return -1;
        }
        buf      += r;
        len      -= (size_t)r;
        in->done += (uint64_t)r;
    }
    progress(in, 0);
    return 0;
}

static int on_end(void *arg, const tar_entry_t *e) {
    install_t *in = arg;
    int r = close(in->fd);
    in->fd = -1;
    if (r < 0) {
        bpm_error("writing %s failed: %s", e->path, strerror(errno));
        return -1;
    }
    return 0;
}

static int finish(install_t *in) {
    char path[BPM_PATH_MAX];
    snprintf(path, sizeof(path), "%s/" PKG_META, in->stage);
    if (mkdir(path, 0755) < 0) {
        bpm_error("cant make %s: %s", path, strerror(errno));
        return -1;
    }
    snprintf(path, sizeof(path), "%s/" PKG_META "/" PKG_INFO, in->stage);
    if (write_file(path, in->pkg.raw, in->pkg.raw_len) < 0) {
        bpm_error("cant write %s: %s", path, strerror(errno));
        return -1;
    }
    snprintf(path, sizeof(path), "%s/" PKG_META "/" PKG_FILES, in->stage);
    if (write_file(path, in->list ? in->list : "", in->list_len) < 0) {
        bpm_error("cant write %s: %s", path, strerror(errno));
        return -1;
    }

    // rename cant go over a folder, so the old one is moved aside until the new one is in
    char aside[BPM_PATH_MAX] = "";
    if (in->old[0] && !strcmp(in->old, in->final)) {
        snprintf(aside, sizeof(aside), "%s/.%s.old", in->pkgs, in->pkg.name);
        remove_tree(aside);
        if (rename(in->old, aside) < 0) {
            bpm_error("cant move the old %s out of the way: %s", in->old, strerror(errno));
            return -1;
        }
    }
    if (rename(in->stage, in->final) < 0) {
        bpm_error("cant move %s to %s: %s", in->stage, in->final, strerror(errno));
        if (aside[0])
            rename(aside, in->old);
        return -1;
    }
    in->staged = 0;

    const char *gone = aside[0] ? aside : in->old;
    if (gone[0] && remove_tree(gone) < 0)
        bpm_error("the new one is in but the old copy in %s couldnt all be deleted: %s", gone, strerror(errno));

    // the old ones links might point at another drive, or at programs this one dropped
    if (in->old[0])
        links_remove(in->old);
    links_add(&in->pkg, in->final);
    return 0;
}

static int install_one(const char *file, const char *root, int force) {
    install_t in;
    memset(&in, 0, sizeof(in));
    in.file     = file;
    in.root_opt = root;
    in.force    = force;
    in.fd       = -1;
    in.shown    = -1;

    bpkg_ops_t ops = { on_info, on_begin, on_data, on_end, &in };
    int r = bpkg_read(file, &ops);
    if (r == 0) {
        progress(&in, 1);
        r = finish(&in);
    }
    if (bpm_line_open) {
        printf("\n");
        bpm_line_open = 0;
    }

    if (in.fd >= 0)
        close(in.fd);
    if (in.staged) {
        printf("cleaning up %s\n", in.stage);
        remove_tree(in.stage);
    }
    if (r == 0) {
        printf("installed %s %s\n", in.pkg.name, in.pkg.version);
        if (in.pkg.exec[0])
            printf("run it with: %s (linked in " LINK_DIR ")\n", in.pkg.exec);
    }

    pkg_free(&in.pkg);
    free(in.list);
    return r;
}

int cmd_install(int argc, char **argv) {
    const char *root = NULL;
    int force = 0, files = 0, status = 0;

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-f")) {
            force = 1;
        } else if (!strcmp(argv[i], "-r")) {
            if (++i == argc) {
                bpm_error("-r needs a drive, like -r /mnt/usb");
                return 2;
            }
            root = argv[i];
        } else if (argv[i][0] == '-') {
            bpm_error("install doesnt know %s", argv[i]);
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
        files++;
        if (install_one(argv[i], root, force) < 0)
            status = 1;
    }

    if (!files) {
        bpm_error("install what? bpm install <file.bpkg>");
        return 2;
    }
    return status;
}
