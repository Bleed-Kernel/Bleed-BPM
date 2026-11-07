#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inflate.h>
#include <bpm.h>

#define BPKG_STOP   -100

typedef struct {
    const bpkg_ops_t *ops;
    const char       *path;
    tar_t            *tar;
    int               fd;
    int               result;   // why the sink stopped

    int               have_info;
    char             *info;
    size_t            info_len;
    pkg_t             pkg;

    int               passing;  // this entry goes to ops
    tar_entry_t       cur;
} bpkg_t;

static long src_read(void *arg, uint8_t *buf, size_t len) {
    bpkg_t *b = arg;
    return read(b->fd, buf, len);
}

static const char *skip_dot(const char *p) {
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    return p;
}

static int rel_ok(const char *rel) {
    if (rel[0] == '/')
        return 0;
    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0 && slash)
            return 0;
        if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.'))
            return 0;
        if (p == rel && len == strlen(PKG_META) && !memcmp(p, PKG_META, len))
            return 0;
        if (!slash)
            break;
        p = slash + 1;
    }
    return 1;
}

static const char *payload_path(const char *path) {
    path = skip_dot(path);
    size_t plen = strlen(PKG_PAYLOAD);
    if (!strncmp(path, PKG_PAYLOAD, plen))
        return path + plen;
    if (!strcmp(path, "files"))
        return "";
    return NULL;
}

static int t_begin(void *arg, const tar_entry_t *e) {
    bpkg_t *b = arg;
    b->passing = 0;

    if (!b->have_info) {
        const char *p = skip_dot(e->path);
        if (!*p && e->type == TAR_DIR)
            return 0;
        if (strcmp(p, PKG_INFO) || e->type != TAR_FILE) {
            bpm_error("%s: not a bpm package (it doesnt start with a PKGINFO)", b->path);
            return -1;
        }
        if (e->size > PKG_INFO_MAX) {
            bpm_error("%s: PKGINFO is too big", b->path);
            return -1;
        }
        b->info = malloc((size_t)e->size + 1);
        b->info_len = 0;
        return b->info ? 1 : -1;
    }

    const char *rel = payload_path(e->path);
    if (!rel || !*rel)
        return 0;
    if (!rel_ok(rel)) {
        bpm_error("%s: refusing a file outside the package: %s", b->path, e->path);
        return -1;
    }

    b->cur = *e;
    strcpy(b->cur.path, rel);
    size_t len = strlen(b->cur.path);
    if (len && b->cur.path[len - 1] == '/')
        b->cur.path[len - 1] = 0;

    if (e->type == TAR_HARDLINK) {
        const char *to = payload_path(e->link);
        if (!to || !*to || !rel_ok(to)) {
            bpm_error("%s: refusing a link outside the package: %s", b->path, e->link);
            return -1;
        }
        strcpy(b->cur.link, to);
    }

    int r = b->ops->begin ? b->ops->begin(b->ops->arg, &b->cur) : 0;
    if (r > 0)
        b->passing = 1;
    return r;
}

static int t_data(void *arg, const uint8_t *buf, size_t len) {
    bpkg_t *b = arg;
    if (!b->have_info) {
        memcpy(b->info + b->info_len, buf, len);
        b->info_len += len;
        return 0;
    }
    return b->passing && b->ops->data ? b->ops->data(b->ops->arg, buf, len) : 0;
}

static int t_end(void *arg, const tar_entry_t *e) {
    (void)e;
    bpkg_t *b = arg;
    if (b->have_info)
        return b->passing && b->ops->end ? b->ops->end(b->ops->arg, &b->cur) : 0;

    b->have_info = 1;
    if (pkg_parse(&b->pkg, b->info, b->info_len) < 0)
        return -1;
    int r = b->ops->info ? b->ops->info(b->ops->arg, &b->pkg) : 0;
    return r == 1 ? BPKG_STOP : r;
}

static int sink(void *arg, const uint8_t *buf, size_t len) {
    bpkg_t *b = arg;
    int r = tar_feed(b->tar, buf, len);
    if (r < 0) {
        b->result = r;
        return 1;
    }
    return 0;
}

int bpkg_read(const char *path, const bpkg_ops_t *ops) {
    bpkg_t b;
    memset(&b, 0, sizeof(b));
    b.ops  = ops;
    b.path = path;
    b.fd   = open(path, O_RDONLY);
    if (b.fd < 0) {
        bpm_error("cant open %s: %s", path, strerror(errno));
        return -1;
    }

    tar_ops_t t = { t_begin, t_data, t_end, &b };
    b.tar = tar_open(&t);
    if (!b.tar) {
        close(b.fd);
        bpm_error("out of memory");
        return -1;
    }

    int status = 0;
    int r = gunzip(src_read, &b, sink, &b);
    if (r == GZ_STOPPED) {
        if (b.result == BPKG_STOP)
            status = 0;
        else if (b.result == -1)
            status = -1;        // already said why
        else {
            bpm_error("%s: %s", path, tar_error(b.result));
            status = -1;
        }
    } else if (r != GZ_OK) {
        if (r == GZ_EFORMAT && !b.have_info)
            bpm_error("%s: not a bpm package", path);
        else
            bpm_error("%s: %s", path, gunzip_error(r));
        status = -1;
    } else if (!b.have_info) {
        bpm_error("%s: not a bpm package (no PKGINFO)", path);
        status = -1;
    } else if ((r = tar_finish(b.tar)) < 0) {
        bpm_error("%s: %s", path, tar_error(r));
        status = -1;
    }

    tar_close(b.tar);
    close(b.fd);
    free(b.info);
    pkg_free(&b.pkg);
    return status;
}
