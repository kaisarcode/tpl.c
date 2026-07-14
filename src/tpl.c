/**
 * tpl.c - Template renderer.
 * Summary: Command line interface for the libtpl renderer.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libtpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define KC_TPL_READ  _read
#define KC_TPL_WRITE _write
#define KC_TPL_STDIN_FD  0
#define KC_TPL_STDOUT_FD 1
typedef long long kc_tpl_ssize_t;
#else
#include <unistd.h>
#define KC_TPL_READ  read
#define KC_TPL_WRITE write
#define KC_TPL_STDIN_FD  STDIN_FILENO
#define KC_TPL_STDOUT_FD STDOUT_FILENO
typedef ssize_t kc_tpl_ssize_t;
#endif

/**
 * Reads from a descriptor until the delimiter byte is encountered.
 * @param fd Source descriptor.
 * @param until Delimiter byte value.
 * @param out Receives the allocated buffer pointer (owned by caller).
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_read_request(int fd, int until, char **out) {
    char *buf = NULL;
    size_t used = 0;
    size_t cap = 0;
    unsigned char c;
    kc_tpl_ssize_t n;

    if (!out) {
        return KC_TPL_ERROR;
    }

    *out = NULL;

    for (;;) {
        n = KC_TPL_READ(fd, &c, 1);

        if (n < 0) {
            free(buf);
            return KC_TPL_ERROR;
        }

        if (n == 0) {
            break;
        }

        if (c == (unsigned char)until) {
            if (used + 1 > cap) {
                cap = cap == 0 ? 256 : cap * 2;
                char *p = (char *)realloc(buf, cap);
                if (!p) {
                    free(buf);
                    return KC_TPL_ERROR;
                }
                buf = p;
            }
            buf[used] = '\0';
            *out = buf;
            return KC_TPL_OK;
        }

        if (used + 2 > cap) {
            cap = cap == 0 ? 256 : cap * 2;
            char *p = (char *)realloc(buf, cap);
            if (!p) {
                free(buf);
                return KC_TPL_ERROR;
            }
            buf = p;
        }

        buf[used++] = c;
    }

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) {
            return KC_TPL_ERROR;
        }
    }

    buf[used] = '\0';
    *out = buf;
    return KC_TPL_OK;
}

/**
 * Stores one key=value command line variable.
 * @param ctx Renderer context.
 * @param pair Key-value pair.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_set_pair(kc_tpl_t *ctx, char *pair) {
    char key[64];
    char *eq;
    size_t key_len;

    if (ctx == NULL || pair == NULL) {
        return KC_TPL_ERROR;
    }

    eq = strchr(pair, '=');
    if (eq == NULL) {
        fprintf(stderr, "tpl: invalid --var format, expected key=value\n");
        return KC_TPL_ERROR;
    }

    key_len = (size_t)(eq - pair);
    if (key_len == 0U || key_len >= sizeof(key)) {
        fprintf(stderr, "tpl: invalid --var key\n");
        return KC_TPL_ERROR;
    }

    memcpy(key, pair, key_len);
    key[key_len] = '\0';
    if (kc_tpl_set_var(ctx, key, eq + 1) != KC_TPL_OK) {
        fprintf(stderr, "tpl: %s\n", kc_tpl_strerror(ctx));
        return KC_TPL_ERROR;
    }

    return KC_TPL_OK;
}

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --root <dir>        Base directory for includes (default: cwd)\n");
    printf("    --var <key=value>   Inject a template variable (repeatable)\n");
    printf("    --until N           Request delimiter byte (default 4)\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Prints command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("tpl build %llu\n", (unsigned long long)kc_tpl_version());
}

/**
 * Parses command line options into a renderer context.
 * @param ctx Renderer context.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure, or 1 when handled.
 */
static int kc_tpl_parse_args(kc_tpl_t *ctx, int argc, char **argv) {
    int i;

    i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 1;
        } else if (strcmp(argv[i], "--root") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "tpl: missing value for --root\n");
                return KC_TPL_ERROR;
            }

            if (kc_tpl_set_root(ctx, argv[i]) != KC_TPL_OK) {
                fprintf(stderr, "tpl: %s\n", kc_tpl_strerror(ctx));
                return KC_TPL_ERROR;
            }
        } else if (strcmp(argv[i], "--var") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "tpl: missing value for --var\n");
                return KC_TPL_ERROR;
            }

            if (kc_tpl_set_pair(ctx, argv[i]) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strcmp(argv[i], "--until") == 0) {
            i++;
        } else {
            fprintf(stderr, "tpl: unknown option '%s'\n", argv[i]);
            return KC_TPL_ERROR;
        }

        i++;
    }

    return KC_TPL_OK;
}

/**
 * Executes the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_tpl_options_t opts = kc_tpl_options_default();
    kc_tpl_t *ctx = NULL;
    char *input = NULL;
    char *output = NULL;
    int parse_rc;
    int i;

    kc_tpl_options_load_env(&opts);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            kc_tpl_options_free(&opts);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            kc_tpl_options_free(&opts);
            return 0;
        }
        if (strcmp(argv[i], "--until") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "tpl: --until requires an argument\n");
                kc_tpl_options_free(&opts);
                return 1;
            }
            char *end;
            long v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || v < 0 || v > 255) {
                fprintf(stderr, "tpl: invalid --until value\n");
                kc_tpl_options_free(&opts);
                return 1;
            }
            opts.until = (int)v;
        }
    }

    if (kc_tpl_open(&ctx, &opts) != KC_TPL_OK) {
        fprintf(stderr, "tpl: out of memory\n");
        kc_tpl_options_free(&opts);
        return 1;
    }

    kc_tpl_listen_signals(ctx);
#ifndef _WIN32
    kc_tpl_listen_signal(ctx, 2);
    kc_tpl_listen_signal(ctx, 15);
#endif

    parse_rc = kc_tpl_parse_args(ctx, argc, argv);
    if (parse_rc == 1) {
        kc_tpl_close(ctx);
        kc_tpl_options_free(&opts);
        return 0;
    }

    if (parse_rc != KC_TPL_OK) {
        kc_tpl_close(ctx);
        kc_tpl_options_free(&opts);
        return 1;
    }

    for (;;) {
        input = NULL;
        output = NULL;

        if (kc_tpl_read_request(KC_TPL_STDIN_FD, opts.until, &input) != KC_TPL_OK) {
            free(input);
            break;
        }
        if (!input || input[0] == '\0') {
            free(input);
            break;
        }

        if (kc_tpl_render_string(ctx, input, &output) != KC_TPL_OK) {
            fprintf(stderr, "tpl: %s\n", kc_tpl_strerror(ctx));
            free(input);
            free(output);
            kc_tpl_close(ctx);
            kc_tpl_options_free(&opts);
            return 1;
        }

        if (KC_TPL_WRITE(KC_TPL_STDOUT_FD, output, strlen(output)) < 0) {
            fprintf(stderr, "tpl: failed to write output\n");
            free(input);
            free(output);
            kc_tpl_close(ctx);
            kc_tpl_options_free(&opts);
            return 1;
        }

        {
            unsigned char delim = (unsigned char)opts.until;
            if (KC_TPL_WRITE(KC_TPL_STDOUT_FD, &delim, 1) != 1) {
                fprintf(stderr, "tpl: failed to write delimiter\n");
                free(input);
                free(output);
                kc_tpl_close(ctx);
                kc_tpl_options_free(&opts);
                return 1;
            }
        }

        free(output);
        free(input);
    }

    kc_tpl_close(ctx);
    kc_tpl_options_free(&opts);
    return 0;
}
