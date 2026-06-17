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

#include "tpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Reads all standard input into an owned string.
 * @param out_text Destination pointer for owned text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_read_stdin(char **out_text) {
    char *data;
    size_t length;
    size_t capacity;
    char chunk[4096];
    size_t count;

    if (out_text == NULL) {
        return KC_TPL_ERROR;
    }

    data = NULL;
    length = 0U;
    capacity = 0U;
    while ((count = fread(chunk, 1, sizeof(chunk), stdin)) > 0U) {
        if (length + count + 1U > capacity) {
            size_t next_capacity;
            char *next_data;

            next_capacity = capacity > 0U ? capacity * 2U : 4096U;
            while (next_capacity < length + count + 1U) {
                next_capacity *= 2U;
            }

            next_data = (char *)realloc(data, next_capacity);
            if (next_data == NULL) {
                free(data);
                return KC_TPL_ERROR;
            }

            data = next_data;
            capacity = next_capacity;
        }

        memcpy(data + length, chunk, count);
        length += count;
    }

    if (ferror(stdin)) {
        free(data);
        return KC_TPL_ERROR;
    }

    if (data == NULL) {
        data = (char *)malloc(1U);
        if (data == NULL) {
            return KC_TPL_ERROR;
        }
    }

    data[length] = '\0';
    *out_text = data;
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
    char *input;
    char *output;
    int parse_rc;
    int status;

    kc_tpl_options_load_env(&opts);

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

    input = NULL;
    output = NULL;
    status = 0;
    if (kc_tpl_read_stdin(&input) != KC_TPL_OK) {
        fprintf(stderr, "tpl: failed to read input\n");
        kc_tpl_close(ctx);
        kc_tpl_options_free(&opts);
        return 1;
    }

    if (kc_tpl_render_string(ctx, input, &output) != KC_TPL_OK) {
        fprintf(stderr, "tpl: %s\n", kc_tpl_strerror(ctx));
        status = 1;
    } else if (fputs(output, stdout) == EOF) {
        fprintf(stderr, "tpl: failed to write output\n");
        status = 1;
    }

    free(output);
    free(input);
    kc_tpl_close(ctx);
    kc_tpl_options_free(&opts);
    return status;
}
