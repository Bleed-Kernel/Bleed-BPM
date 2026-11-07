#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bpm.h>

int pkg_name_ok(const char *name) {
    size_t len = strlen(name);
    if (!len || len >= PKG_NAME_MAX)
        return 0;
    if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
        return 0;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '+';
        if (!ok)
            return 0;
    }
    return 1;
}

static void set(char *out, size_t size, const char *val, size_t len) {
    if (len >= size)
        len = size - 1;
    memcpy(out, val, len);
    out[len] = 0;
}

int pkg_parse(pkg_t *p, const char *text, size_t len) {
    memset(p, 0, sizeof(*p));
    p->raw = malloc(len + 1);
    if (!p->raw)
        return -1;
    memcpy(p->raw, text, len);
    p->raw[len] = 0;
    p->raw_len = len;

    const char *s   = p->raw;
    const char *end = p->raw + len;
    while (s < end) {
        const char *nl   = memchr(s, '\n', (size_t)(end - s));
        const char *stop = nl ? nl : end;
        const char *eq   = memchr(s, '=', (size_t)(stop - s));

        if (*s != '#' && eq) {
            const char *k  = s, *ke = eq;
            const char *v  = eq + 1, *ve = stop;
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
            while (v < ve && (*v == ' ' || *v == '\t')) v++;
            while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
            size_t klen = (size_t)(ke - k), vlen = (size_t)(ve - v);

            if (klen == 4 && !memcmp(k, "name", 4))
                set(p->name, sizeof(p->name), v, vlen);
            else if (klen == 7 && !memcmp(k, "version", 7))
                set(p->version, sizeof(p->version), v, vlen);
            else if (klen == 11 && !memcmp(k, "description", 11))
                set(p->description, sizeof(p->description), v, vlen);
            else if (klen == 4 && !memcmp(k, "exec", 4))
                set(p->exec, sizeof(p->exec), v, vlen);
            else if (klen == 4 && !memcmp(k, "size", 4))
                p->size = strtoull(v, NULL, 10);
            else if (klen == 5 && !memcmp(k, "files", 5))
                p->files = strtoull(v, NULL, 10);
        }
        s = stop + 1;
    }

    if (!p->name[0] || !p->version[0]) {
        bpm_error("PKGINFO has no %s", p->name[0] ? "version" : "name");
        return -1;
    }
    if (!pkg_name_ok(p->name)) {
        bpm_error("'%s' isnt a usable package name (a-z 0-9 - _ . +)", p->name);
        return -1;
    }
    return 0;
}

void pkg_free(pkg_t *p) {
    free(p->raw);
    p->raw = NULL;
}

void pkg_print(const pkg_t *p, const char *where) {
    char size[16];
    human_size(p->size, size);
    printf("name         %s\n", p->name);
    printf("version      %s\n", p->version);
    if (p->description[0])
        printf("description  %s\n", p->description);
    if (p->exec[0])
        printf("programs     %s\n", p->exec);
    printf("size         %s in %llu files\n", size, (unsigned long long)p->files);
    if (where)
        printf("installed    %s\n", where);
}
