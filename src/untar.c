#include <stdlib.h>
#include <string.h>
#include <untar.h>

#define BLOCK       512
#define META_MAX    (64 * 1024)     // a pax header or long name bigger than this is junk

enum {
    ST_HEADER,
    ST_DATA,
    ST_PAD,
    ST_DONE,
};

enum {
    TO_CALLER,
    TO_SKIP,
    TO_PAX,
    TO_LONGNAME,
    TO_LONGLINK,
};

struct tar {
    tar_ops_t   ops;
    int         state;
    uint8_t     hdr[BLOCK];
    size_t      hdr_len;
    int         zeros;          // zero blocks in a row, two is the end

    tar_entry_t entry;
    int         to;
    uint64_t    left;           // data bytes left in this entry
    size_t      pad;

    char       *meta;           // pax header or long name being collected
    size_t      meta_len;

    // what the last pax header or long name said about the next entry
    char        next_path[TAR_PATH_MAX];
    char        next_link[TAR_PATH_MAX];
    int         next_size_set;
    uint64_t    next_size;
};

tar_t *tar_open(const tar_ops_t *ops) {
    tar_t *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->ops  = *ops;
    t->meta = malloc(META_MAX + 1);
    if (!t->meta) {
        free(t);
        return NULL;
    }
    return t;
}

void tar_close(tar_t *t) {
    if (!t)
        return;
    free(t->meta);
    free(t);
}

static uint64_t parse_octal(const uint8_t *p, size_t n) {
    // gnu base-256 for sizes that dont fit in octal
    if (p[0] & 0x80) {
        uint64_t v = p[0] & 0x7F;
        for (size_t i = 1; i < n; i++)
            v = (v << 8) | p[i];
        return v;
    }

    uint64_t v = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0))
        i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
        v = v * 8 + (uint64_t)(p[i] - '0');
    return v;
}

static int checksum_ok(const uint8_t *h) {
    uint64_t want = parse_octal(h + 148, 8);
    uint64_t sum  = 0;
    int64_t  ssum = 0;
    for (int i = 0; i < BLOCK; i++) {
        uint8_t c = (i >= 148 && i < 156) ? ' ' : h[i];
        sum  += c;
        ssum += (int8_t)c;
    }
    // some old tars summed signed bytes
    return sum == want || (uint64_t)ssum == want;
}

// copies a field that isnt always nul terminated
static size_t field(char *out, const uint8_t *p, size_t n) {
    size_t len = 0;
    while (len < n && p[len])
        len++;
    memcpy(out, p, len);
    out[len] = 0;
    return len;
}

static int set_path(char out[TAR_PATH_MAX], const char *s, size_t len) {
    if (len >= TAR_PATH_MAX)
        return TAR_ENAME;
    memcpy(out, s, len);
    out[len] = 0;
    return 0;
}

// "len key=value\n" records
static int parse_pax(tar_t *t) {
    char *p   = t->meta;
    char *end = t->meta + t->meta_len;

    while (p < end) {
        char *q = p;
        size_t rec = 0;
        while (q < end && *q >= '0' && *q <= '9')
            rec = rec * 10 + (size_t)(*q++ - '0');
        if (q >= end || *q != ' ' || rec == 0 || rec > (size_t)(end - p))
            return TAR_ECORRUPT;

        char *key  = q + 1;
        char *stop = p + rec - 1;       // the newline
        char *eq   = memchr(key, '=', (size_t)(stop - key));
        if (!eq)
            return TAR_ECORRUPT;

        size_t klen = (size_t)(eq - key);
        char  *val  = eq + 1;
        size_t vlen = (size_t)(stop - val);
        int r = 0;
        if (klen == 4 && !memcmp(key, "path", 4)) {
            r = set_path(t->next_path, val, vlen);
        } else if (klen == 8 && !memcmp(key, "linkpath", 8)) {
            r = set_path(t->next_link, val, vlen);
        } else if (klen == 4 && !memcmp(key, "size", 4)) {
            uint64_t v = 0;
            for (size_t i = 0; i < vlen && val[i] >= '0' && val[i] <= '9'; i++)
                v = v * 10 + (uint64_t)(val[i] - '0');
            t->next_size     = v;
            t->next_size_set = 1;
        }
        if (r < 0)
            return r;
        p += rec;
    }
    return 0;
}

static int header(tar_t *t) {
    const uint8_t *h = t->hdr;

    int zero = 1;
    for (int i = 0; i < BLOCK; i++) {
        if (h[i]) {
            zero = 0;
            break;
        }
    }
    if (zero) {
        if (++t->zeros == 2)
            t->state = ST_DONE;
        return 0;
    }
    t->zeros = 0;

    if (!checksum_ok(h))
        return TAR_ECORRUPT;

    tar_entry_t *e = &t->entry;
    memset(e, 0, sizeof(*e));
    e->type = h[156] ? (char)h[156] : TAR_FILE;
    e->size = parse_octal(h + 124, 12);

    switch (e->type) {
        case 'x':
        case 'L':
        case 'K':
            if (e->size > META_MAX)
                return TAR_ECORRUPT;
            t->to = e->type == 'x' ? TO_PAX : e->type == 'L' ? TO_LONGNAME : TO_LONGLINK;
            t->meta_len = 0;
            break;

        case 'g':
            t->to = TO_SKIP;
            break;

        default: {
            if (t->next_path[0]) {
                strcpy(e->path, t->next_path);
            } else {
                char name[101], prefix[156];
                size_t nlen = field(name, h, 100);
                size_t plen = memcmp(h + 257, "ustar", 5) ? 0 : field(prefix, h + 345, 155);
                if (plen + nlen + 2 > TAR_PATH_MAX)
                    return TAR_ENAME;
                if (plen) {
                    memcpy(e->path, prefix, plen);
                    e->path[plen] = '/';
                    memcpy(e->path + plen + 1, name, nlen + 1);
                } else {
                    memcpy(e->path, name, nlen + 1);
                }
            }

            if (t->next_link[0]) {
                strcpy(e->link, t->next_link);
            } else {
                char link[101];
                field(link, h + 157, 100);
                strcpy(e->link, link);
            }
            if (t->next_size_set)
                e->size = t->next_size;

            t->next_path[0]  = 0;
            t->next_link[0]  = 0;
            t->next_size_set = 0;

            // links and dirs carry no data whatever the size field says
            if (e->type == TAR_DIR || e->type == TAR_SYMLINK || e->type == TAR_HARDLINK)
                e->size = 0;

            int r = t->ops.begin ? t->ops.begin(t->ops.arg, e) : 0;
            if (r < 0)
                return r;
            t->to = r > 0 ? TO_CALLER : TO_SKIP;
            break;
        }
    }

    t->left  = e->size;
    t->pad   = (size_t)((BLOCK - (e->size % BLOCK)) % BLOCK);
    t->state = ST_DATA;
    return 0;
}

// the entrys data is all in, tell whoever wanted it
static int entry_done(tar_t *t) {
    int r = 0;
    switch (t->to) {
        case TO_CALLER:
            if (t->ops.end)
                r = t->ops.end(t->ops.arg, &t->entry);
            break;
        case TO_PAX:
            r = parse_pax(t);
            break;
        case TO_LONGNAME:
        case TO_LONGLINK: {
            size_t len = strnlen(t->meta, t->meta_len);
            r = set_path(t->to == TO_LONGNAME ? t->next_path : t->next_link, t->meta, len);
            break;
        }
    }
    t->state = t->pad ? ST_PAD : ST_HEADER;
    t->hdr_len = 0;
    return r;
}

int tar_feed(tar_t *t, const uint8_t *buf, size_t len) {
    while (len) {
        switch (t->state) {
            case ST_HEADER: {
                size_t n = BLOCK - t->hdr_len;
                if (n > len)
                    n = len;
                memcpy(t->hdr + t->hdr_len, buf, n);
                t->hdr_len += n;
                buf += n;
                len -= n;
                if (t->hdr_len < BLOCK)
                    break;

                t->hdr_len = 0;
                int r = header(t);
                if (r < 0)
                    return r;
                // an empty entry is done as soon as its header is
                if (t->state == ST_DATA && !t->left) {
                    r = entry_done(t);
                    if (r < 0)
                        return r;
                }
                break;
            }

            case ST_DATA: {
                size_t n = t->left < len ? (size_t)t->left : len;
                int r = 0;
                if (t->to == TO_CALLER) {
                    if (t->ops.data)
                        r = t->ops.data(t->ops.arg, buf, n);
                } else if (t->to != TO_SKIP) {
                    memcpy(t->meta + t->meta_len, buf, n);
                    t->meta_len += n;
                }
                if (r < 0)
                    return r;
                buf += n;
                len -= n;
                t->left -= n;
                if (!t->left) {
                    r = entry_done(t);
                    if (r < 0)
                        return r;
                }
                break;
            }

            case ST_PAD: {
                size_t n = t->pad < len ? t->pad : len;
                buf += n;
                len -= n;
                t->pad -= n;
                if (!t->pad)
                    t->state = ST_HEADER;
                break;
            }

            case ST_DONE:
                // tar pads the end out to a whole record, none of it matters
                return 0;
        }
    }
    return 0;
}

int tar_finish(tar_t *t) {
    if (t->state == ST_DONE)
        return 0;
    // one zero block then the end is fine, plenty of writers stop there
    if (t->state == ST_HEADER && t->hdr_len == 0 && t->zeros)
        return 0;
    return TAR_ETRUNC;
}

const char *tar_error(int r) {
    switch (r) {
        case TAR_ECORRUPT: return "the archive inside is damaged";
        case TAR_ENAME:    return "a file name is too long";
        case TAR_ENOMEM:   return "out of memory";
        case TAR_ETRUNC:   return "the archive inside is cut short";
    }
    return "unknown error";
}
