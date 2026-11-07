#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <bpm.h>

typedef struct {
    const char *name;
    const char *alias;
    int       (*fn)(int argc, char **argv);
} command_t;

static const command_t commands[] = {
    { "install", "i",         cmd_install },
    { "remove",  "uninstall", cmd_remove  },
    { "list",    "ls",        cmd_list    },
    { "info",    NULL,        cmd_info    },
    { "files",   NULL,        cmd_files   },
    { "verify",  NULL,        cmd_verify  },
    { "link",    NULL,        cmd_link    },
};

int bpm_line_open;

void bpm_error(const char *fmt, ...) {
    if (bpm_line_open) {
        printf("\n");
        bpm_line_open = 0;
    }
    fflush(stdout);

    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "bpm: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void usage(FILE *out) {
    fprintf(out,
        "bpm " BPM_VERSION ", the bleed package manager\n"
        "\n"
        "usage: bpm <command> ...\n"
        "\n"
        "  install <file.bpkg>...   install packages into /opt, -f to replace one thats already in,\n"
        "                           -r <drive> to put it on a drive instead (like -r /mnt/usb)\n"
        "  remove <name>...         remove packages, -p deletes files bpm didnt put there too\n"
        "  list                     whats installed and where\n"
        "  info <name|file.bpkg>    what a package is\n"
        "  files <name>             what an installed package put on the drive\n"
        "  verify <file.bpkg>       check a package isnt damaged without installing it\n"
        "  link                     link every installed packages programs into " LINK_DIR " again\n"
        "\n"
        "packages go in /" PKG_DIR "/<name>, or <drive>/" PKG_DIR "/<name> with -r\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        usage(stdout);
        return 0;
    }
    if (!strcmp(argv[1], "--version")) {
        printf("bpm " BPM_VERSION "\n");
        return 0;
    }

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        const command_t *c = &commands[i];
        if (!strcmp(argv[1], c->name) || (c->alias && !strcmp(argv[1], c->alias)))
            return c->fn(argc - 2, argv + 2);
    }

    bpm_error("no command called %s, bpm help lists them", argv[1]);
    return 2;
}
