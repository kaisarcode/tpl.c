/**
 * tpl.c - Atomic Template Renderer
 * Summary: Renders template strings with includes, blocks, and scoped data.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include <ctype.h>
#include <limits.h>
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

#define KC_TPL_VERSION "0.1.0"

enum { KC_TPL_EVAL_CAP = 2048 };

typedef struct { char key[64]; char *val; } kc_tpl_var_t;
typedef struct { char name[64]; char body[8192]; } kc_tpl_block_t;
typedef struct kc_tpl_scope {
    kc_tpl_var_t vars[128];
    int var_n;
    kc_tpl_block_t blocks[64];
    int block_n;
    struct kc_tpl_scope *parent;
} kc_tpl_scope_t;

static char kc_tpl_root[4096];
static char kc_tpl_out[262144];
static size_t kc_tpl_out_n;

static const char *kc_tpl_comment_open  = "{{/" "*";
static const char *kc_tpl_comment_close = "*" "/}}";

/**
 * Trims leading and trailing whitespace in place.
 * @param s Mutable string pointer.
 * @return Pointer to trimmed content.
 */
static char *kc_tpl_trim(char *s) {
    char *e;

    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/**
 * Copies a bounded substring into a destination buffer.
 * @param dst Destination buffer.
 * @param cap Destination capacity.
 * @param src Source text.
 * @param len Number of bytes to copy.
 * @return None.
 */
static void kc_tpl_copy(char *dst, size_t cap, const char *src, size_t len) {
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/**
 * Appends text to the output buffer.
 * @param s Source text.
 * @return None.
 */
static void kc_tpl_put(const char *s) {
    size_t n = strlen(s);

    if (kc_tpl_out_n + n >= sizeof(kc_tpl_out)) {
        fprintf(stderr, "tpl: output too large\n");
        exit(1);
    }
    memcpy(kc_tpl_out + kc_tpl_out_n, s, n);
    kc_tpl_out_n += n;
    kc_tpl_out[kc_tpl_out_n] = 0;
}

/**
 * Appends HTML-escaped text to the output buffer.
 * @param s Source text.
 * @return None.
 */
static void kc_tpl_put_esc(const char *s) {
    for (; *s; s++) {
        if (*s == '&')      kc_tpl_put("&amp;");
        else if (*s == '<') kc_tpl_put("&lt;");
        else if (*s == '>') kc_tpl_put("&gt;");
        else if (*s == '"') kc_tpl_put("&quot;");
        else { char c[2] = {*s, 0}; kc_tpl_put(c); }
    }
}

/**
 * Duplicates a null-terminated string.
 * @param s Source text.
 * @return Owned copy, exits on allocation failure.
 */
static char *kc_tpl_dup(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);

    if (!out) {
        fprintf(stderr, "tpl: out of memory\n");
        exit(1);
    }
    memcpy(out, s, len + 1);
    return out;
}

/**
 * Resolves a variable pointer from the scope chain.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @return Value pointer or NULL when not found.
 */
static const char *kc_tpl_var_find(kc_tpl_scope_t *scope, const char *key) {
    for (; scope; scope = scope->parent) {
        for (int i = 0; i < scope->var_n; i++) {
            if (strcmp(scope->vars[i].key, key) == 0) return scope->vars[i].val;
        }
    }
    return NULL;
}

/**
 * Resolves a variable from the scope chain, returning empty string when absent.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @return Value or empty string.
 */
static const char *kc_tpl_var_get(kc_tpl_scope_t *scope, const char *key) {
    const char *v = kc_tpl_var_find(scope, key);

    return v ? v : "";
}

/**
 * Stores or updates a variable in the current scope.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @param val Variable value.
 * @return None.
 */
static void kc_tpl_var_set(kc_tpl_scope_t *scope, const char *key, const char *val) {
    int i;

    for (i = 0; i < scope->var_n; i++) {
        if (strcmp(scope->vars[i].key, key) == 0) break;
    }
    if (i == scope->var_n) scope->var_n++;
    snprintf(scope->vars[i].key, sizeof(scope->vars[i].key), "%s", key);
    free(scope->vars[i].val);
    scope->vars[i].val = kc_tpl_dup(val);
}

/**
 * Releases dynamic variable allocations in one scope level.
 * @param scope Scope pointer.
 * @return None.
 */
static void kc_tpl_scope_clear(kc_tpl_scope_t *scope) {
    for (int i = 0; i < scope->var_n; i++) {
        free(scope->vars[i].val);
        scope->vars[i].val = NULL;
    }
    scope->var_n = 0;
}

/**
 * Resolves a block body from the scope chain.
 * @param scope Scope pointer.
 * @param name Block name.
 * @return Block body or NULL when not found.
 */
static const char *kc_tpl_block_get(kc_tpl_scope_t *scope, const char *name) {
    for (; scope; scope = scope->parent) {
        for (int i = 0; i < scope->block_n; i++) {
            if (strcmp(scope->blocks[i].name, name) == 0) return scope->blocks[i].body;
        }
    }
    return NULL;
}

/**
 * Stores or updates a block in the current scope.
 * @param scope Scope pointer.
 * @param name Block name.
 * @param body Block body.
 * @return None.
 */
static void kc_tpl_block_set(kc_tpl_scope_t *scope, const char *name, const char *body) {
    int i;

    for (i = 0; i < scope->block_n; i++) {
        if (strcmp(scope->blocks[i].name, name) == 0) break;
    }
    if (i == scope->block_n) scope->block_n++;
    snprintf(scope->blocks[i].name, sizeof(scope->blocks[i].name), "%s", name);
    snprintf(scope->blocks[i].body, sizeof(scope->blocks[i].body), "%s", body);
}

/**
 * Resolves dot-notation expressions into flat underscore-joined keys.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @return Owned resolved value or NULL when unresolved.
 */
static char *kc_tpl_eval_dot(kc_tpl_scope_t *scope, const char *expr) {
    char path[512];
    char left[128];
    char right[384];
    char tail[384];
    char key[4096];
    char *dot;
    char *tok;
    char *save;
    const char *found;
    const char *alias;

    if (!strchr(expr, '.')) return NULL;
    snprintf(path, sizeof(path), "%s", expr);
    dot = strchr(path, '.');
    if (!dot || dot == path || dot[1] == 0) return NULL;
    *dot = 0;
    snprintf(left, sizeof(left), "%s", kc_tpl_trim(path));
    snprintf(right, sizeof(right), "%s", kc_tpl_trim(dot + 1));
    if (left[0] == 0 || right[0] == 0) return NULL;
    key[0] = 0;
    strncat(key, left, sizeof(key) - strlen(key) - 1);
    snprintf(tail, sizeof(tail), "%s", right);
    save = tail;
    while ((tok = strtok(save, "."))) {
        save = NULL;
        strncat(key, "_", sizeof(key) - strlen(key) - 1);
        strncat(key, kc_tpl_trim(tok), sizeof(key) - strlen(key) - 1);
    }
    found = kc_tpl_var_find(scope, key);
    if (found) return kc_tpl_dup(found);
    alias = kc_tpl_var_find(scope, left);
    if (alias && alias[0]) {
        key[0] = 0;
        strncat(key, alias, sizeof(key) - strlen(key) - 1);
        snprintf(tail, sizeof(tail), "%s", right);
        save = tail;
        while ((tok = strtok(save, "."))) {
            save = NULL;
            strncat(key, "_", sizeof(key) - strlen(key) - 1);
            strncat(key, kc_tpl_trim(tok), sizeof(key) - strlen(key) - 1);
        }
        found = kc_tpl_var_find(scope, key);
        if (found) return kc_tpl_dup(found);
    }
    return NULL;
}

/**
 * Evaluates a template expression to an owned string.
 * @param scope Scope pointer.
 * @param expr Expression text (mutable).
 * @return Owned evaluated value.
 */
static char *kc_tpl_eval_dup(kc_tpl_scope_t *scope, char *expr) {
    char *s = kc_tpl_trim(expr);
    size_t n = strlen(s);
    char *out;

    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        out = malloc(n - 1);
        if (!out) { fprintf(stderr, "tpl: out of memory\n"); exit(1); }
        memcpy(out, s + 1, n - 2);
        out[n - 2] = 0;
        return out;
    }
    if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 || strcmp(s, "null") == 0) {
        return kc_tpl_dup(s);
    }
    out = kc_tpl_eval_dot(scope, s);
    if (out) return out;
    return kc_tpl_dup(kc_tpl_var_get(scope, s));
}

/**
 * Evaluates an expression into a bounded output buffer.
 * @param scope Scope pointer.
 * @param expr Expression text (mutable).
 * @param out Destination buffer (KC_TPL_EVAL_CAP bytes).
 * @return None.
 */
static void kc_tpl_eval(kc_tpl_scope_t *scope, char *expr, char *out) {
    char *val = kc_tpl_eval_dup(scope, expr);

    snprintf(out, KC_TPL_EVAL_CAP, "%s", val);
    free(val);
}

/**
 * Evaluates truthiness of an expression.
 * @param scope Scope pointer.
 * @param expr Expression text (mutable).
 * @return 1 when truthy, 0 otherwise.
 */
static int kc_tpl_truth(kc_tpl_scope_t *scope, char *expr) {
    char value[KC_TPL_EVAL_CAP];

    kc_tpl_eval(scope, expr, value);
    return (
        value[0] &&
        strcmp(value, "0")     != 0 &&
        strcmp(value, "false") != 0 &&
        strcmp(value, "null")  != 0
    );
}

/**
 * Splits a list expression into comma-separated items.
 * @param text List text (mutable).
 * @param out Output item matrix.
 * @return Number of items parsed.
 */
static int kc_tpl_list(char *text, char out[64][128]) {
    int n = 0;
    char buf[2048];

    snprintf(buf, sizeof(buf), "%s", kc_tpl_trim(text));
    if (buf[0] == '[' && buf[strlen(buf) - 1] == ']') {
        memmove(buf, buf + 1, strlen(buf));
        buf[strlen(buf) - 1] = 0;
    }
    for (char *p = strtok(buf, ","); p && n < 64; p = strtok(NULL, ",")) {
        char *s = kc_tpl_trim(p);
        size_t m = strlen(s);

        if (m >= 2 && s[0] == '"' && s[m - 1] == '"') {
            s[m - 1] = 0;
            s++;
        }
        snprintf(out[n++], 128, "%s", s);
    }
    return n;
}

/**
 * Loads a file relative to the configured include root.
 * @param path Relative include path.
 * @param out Destination buffer (8192 bytes).
 * @return 0 on success, 1 on failure.
 */
static int kc_tpl_load(const char *path, char *out) {
    char full[8192];
    FILE *fp;
    size_t n;

    snprintf(full, sizeof(full), "%s/%s", kc_tpl_root, path);
    fp = fopen(full, "rb");
    if (!fp) return 1;
    n = fread(out, 1, 8191, fp);
    fclose(fp);
    out[n] = 0;
    return 0;
}

/**
 * Advances past HTML comments, returning the next {{ tag or NULL.
 * @param tpl Template cursor.
 * @return Pointer to next tag or NULL.
 */
static char *kc_tpl_next_tag(char *tpl) {
    char *p = tpl;

    while (*p) {
        char *tag     = strstr(p, "{{");
        char *comment = strstr(p, "<!--");

        if (!comment) return tag;
        if (!tag || comment < tag) {
            char *end = strstr(comment + 4, "-->");

            if (!end) return tag;
            p = end + 3;
            continue;
        }
        return tag;
    }
    return NULL;
}

/**
 * Finds the matching close directive, respecting nesting and comments.
 * @param tpl Template text after the opening tag's }}.
 * @param open Opening directive keyword.
 * @param close Closing directive keyword.
 * @param alt Optional else-style alternate keyword.
 * @param mid Output pointer set to the alternate position when found.
 * @return Pointer to the closing tag or NULL when not found.
 */
static char *kc_tpl_find(char *tpl, const char *open, const char *close, const char *alt, char **mid) {
    int depth = 1;
    char *p = tpl;

    *mid = NULL;
    while ((p = kc_tpl_next_tag(p))) {
        if (strncmp(p, kc_tpl_comment_open, 4) == 0) {
            char *end = strstr(p + 4, kc_tpl_comment_close);

            if (!end) { fprintf(stderr, "tpl: unterminated comment\n"); exit(1); }
            p = end + 4;
            continue;
        }
        if (strncmp(p, "{{@", 3) != 0) { p += 2; continue; }
        if (strncmp(p + 3, open, strlen(open)) == 0) {
            depth++;
        } else if (alt && depth == 1 && strncmp(p + 3, alt, strlen(alt)) == 0 && !*mid) {
            *mid = p;
        } else if (strncmp(p + 3, close, strlen(close)) == 0 && --depth == 0) {
            return p;
        }
        p += 3;
    }
    return NULL;
}

static void kc_tpl_render(kc_tpl_scope_t *scope, char *tpl);

/**
 * Renders a template string with recursive scope-aware directive evaluation.
 * @param scope Scope pointer.
 * @param tpl Template text (mutable).
 * @return None.
 */
static void kc_tpl_render(kc_tpl_scope_t *scope, char *tpl) {
    char *p = tpl;

    while (*p) {
        char *a = kc_tpl_next_tag(p);

        if (!a) { kc_tpl_put(p); return; }
        if (a > p) {
            char keep = *a;
            *a = 0;
            kc_tpl_put(p);
            *a = keep;
        }
        if (strncmp(a, kc_tpl_comment_open, 4) == 0) {
            char *end = strstr(a + 4, kc_tpl_comment_close);

            if (!end) { fprintf(stderr, "tpl: unterminated comment\n"); exit(1); }
            p = end + 4;
            continue;
        }
        if (strncmp(a, "{{{", 3) == 0) {
            char *b = strstr(a + 3, "}}}");
            char expr[512];
            char *val;

            if (!b) { fprintf(stderr, "tpl: unterminated raw tag\n"); exit(1); }
            kc_tpl_copy(expr, sizeof(expr), a + 3, (size_t)(b - (a + 3)));
            val = kc_tpl_eval_dup(scope, expr);
            kc_tpl_put(val);
            free(val);
            p = b + 3;
            continue;
        }
        {
            char *b = strstr(a + 2, "}}");
            char tag[512];
            char expr[512];
            char val[KC_TPL_EVAL_CAP];

            if (!b) { fprintf(stderr, "tpl: unterminated tag\n"); exit(1); }
            kc_tpl_copy(tag, sizeof(tag), a + 2, (size_t)(b - (a + 2)));
            if (tag[0] != '@') {
                char *text = kc_tpl_eval_dup(scope, tag);

                kc_tpl_put_esc(text);
                free(text);
                p = b + 2;
                continue;
            }
            if (strncmp(tag + 1, "include ", 8) == 0) {
                char file[4096];
                char data[8192];
                char inc_expr[512];

                snprintf(inc_expr, sizeof(inc_expr), "%s", kc_tpl_trim(tag + 9));
                kc_tpl_eval(scope, inc_expr, file);
                if (kc_tpl_load(file, data) != 0) {
                    fprintf(stderr, "tpl: include not found: %s\n", file);
                    exit(1);
                }
                kc_tpl_render(scope, data);
            } else if (strncmp(tag + 1, "var ", 4) == 0) {
                char name[64];

                sscanf(tag + 5, "%63s", name);
                snprintf(expr, sizeof(expr), "%s", strstr(tag + 5, name) + strlen(name));
                kc_tpl_eval(scope, expr, val);
                kc_tpl_var_set(scope, name, val);
            } else if (strncmp(tag + 1, "setblock ", 9) == 0) {
                char name[64];
                char body[8192];
                char *mid;
                char *end;

                sscanf(tag + 10, "%63s", name);
                end = kc_tpl_find(b + 2, "setblock ", "endsetblock", NULL, &mid);
                if (!end) { fprintf(stderr, "tpl: missing endsetblock\n"); exit(1); }
                kc_tpl_copy(body, sizeof(body), b + 2, (size_t)(end - (b + 2)));
                kc_tpl_block_set(scope, name, body);
                p = end + 16;
                continue;
            } else if (strncmp(tag + 1, "block ", 6) == 0) {
                char name[64];
                char props[512];
                kc_tpl_scope_t child = {0};
                char *body;

                child.parent = scope;
                sscanf(tag + 7, "%63s", name);
                body = (char *)kc_tpl_block_get(scope, name);
                if (!body) { p = b + 2; continue; }
                {
                    char *l = strchr(tag, '{');
                    char *r = strrchr(tag, '}');
                    char open = '{';
                    char close = '}';

                    if (!l || !r || r <= l) {
                        l = strchr(tag, '[');
                        r = strrchr(tag, ']');
                        open = '[';
                        close = ']';
                    }
                    if (l && r && r > l && l[0] == open && r[0] == close) {
                        char *start = l + 1;

                        snprintf(props, (size_t)(r - start + 1), "%s", start);
                        for (char *q = strtok(props, ","); q; q = strtok(NULL, ",")) {
                            char key[64];
                            char *k;
                            char *c = strchr(q, ':');

                            if (!c) continue;
                            *c = 0;
                            k = kc_tpl_trim(q);
                            if (k[0] == '"') {
                                size_t kn = strlen(k);
                                if (kn >= 2 && k[kn - 1] == '"') { k[kn - 1] = 0; k++; }
                            }
                            snprintf(key, sizeof(key), "%s", k);
                            snprintf(expr, sizeof(expr), "%s", c + 1);
                            kc_tpl_eval(scope, expr, val);
                            kc_tpl_var_set(&child, key, val);
                        }
                    }
                }
                kc_tpl_render(&child, body);
                kc_tpl_scope_clear(&child);
            } else if (strncmp(tag + 1, "if ", 3) == 0) {
                char body[8192];
                char alt_body[8192];
                char *mid;
                char *end;

                end = kc_tpl_find(b + 2, "if ", "endif", "else", &mid);
                if (!end) { fprintf(stderr, "tpl: missing endif\n"); exit(1); }
                if (mid) {
                    kc_tpl_copy(body,     sizeof(body),     b + 2,   (size_t)(mid - (b + 2)));
                    kc_tpl_copy(alt_body, sizeof(alt_body), mid + 9, (size_t)(end - (mid + 9)));
                } else {
                    kc_tpl_copy(body, sizeof(body), b + 2, (size_t)(end - (b + 2)));
                    alt_body[0] = 0;
                }
                kc_tpl_render(scope, kc_tpl_truth(scope, tag + 4) ? body : alt_body);
                p = end + 10;
                continue;
            } else if (strncmp(tag + 1, "foreach ", 8) == 0) {
                char item[64];
                char list_expr[256];
                char body[8192];
                char items[64][128];
                char *in;
                char *mid;
                char *end;
                kc_tpl_scope_t child;
                int n;

                snprintf(expr, sizeof(expr), "%s", tag + 9);
                in = strstr(expr, " in ");
                if (!in) { fprintf(stderr, "tpl: invalid foreach\n"); exit(1); }
                *in = 0;
                snprintf(item, sizeof(item), "%s", kc_tpl_trim(expr));
                snprintf(list_expr, sizeof(list_expr), "%s", kc_tpl_trim(in + 4));
                kc_tpl_eval(scope, list_expr, val);
                n = kc_tpl_list(val, items);
                end = kc_tpl_find(b + 2, "foreach ", "endforeach", NULL, &mid);
                if (!end) { fprintf(stderr, "tpl: missing endforeach\n"); exit(1); }
                kc_tpl_copy(body, sizeof(body), b + 2, (size_t)(end - (b + 2)));
                for (int i = 0; i < n; i++) {
                    memset(&child, 0, sizeof(child));
                    child.parent = scope;
                    kc_tpl_var_set(&child, item, items[i]);
                    kc_tpl_render(&child, body);
                    kc_tpl_scope_clear(&child);
                }
                p = end + 15;
                continue;
            }
            p = b + 2;
        }
    }
}

/**
 * Reads all bytes from a file descriptor into an owned buffer.
 * @param fd Source descriptor.
 * @return Owned null-terminated buffer or NULL on failure.
 */
static char *kc_tpl_read_fd(int fd) {
    size_t cap = 4096;
    size_t used = 0;
    char *buf = malloc(cap + 1);

    if (!buf) return NULL;
    while (1) {
        kc_tpl_ssize_t n;

        if (used == cap) {
            char *next = realloc(buf, cap * 2 + 1);

            if (!next) { free(buf); return NULL; }
            buf = next;
            cap *= 2;
        }
        n = KC_TPL_READ(fd, buf + used, cap - used);
        if (n < 0) { free(buf); return NULL; }
        if (n == 0) break;
        used += (size_t)n;
    }
    buf[used] = 0;
    return buf;
}

/**
 * Writes a full buffer to a file descriptor.
 * @param fd Destination descriptor.
 * @param text Output text.
 * @return 0 on success, 1 on failure.
 */
static int kc_tpl_write_fd(int fd, const char *text) {
    size_t len = strlen(text);
    size_t off = 0;

    while (off < len) {
        kc_tpl_ssize_t n = KC_TPL_WRITE(fd, text + off, len - off);

        if (n <= 0) return 1;
        off += (size_t)n;
    }
    return 0;
}

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --root <dir>        Base directory for includes (default: cwd)\n");
    printf("    --var <key=value>   Inject a template variable (repeatable)\n");
    printf("    -h, --help          Show this help message\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("tpl %s\n", KC_TPL_VERSION);
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_tpl_scope_t scope = {0};
    char *input = NULL;
    int i = 1;

    if (!getcwd(kc_tpl_root, sizeof(kc_tpl_root))) return 1;

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 0;
        } else if (strcmp(argv[i], "--root") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "tpl: missing value for --root\n");
                return 1;
            }
            snprintf(kc_tpl_root, sizeof(kc_tpl_root), "%s", argv[i]);
        } else if (strcmp(argv[i], "--var") == 0) {
            char key[64];
            char *pair;
            char *eq;
            size_t key_len;

            if (++i >= argc) {
                fprintf(stderr, "tpl: missing value for --var\n");
                return 1;
            }
            pair = argv[i];
            eq = strchr(pair, '=');
            if (!eq) {
                fprintf(stderr, "tpl: invalid --var format, expected key=value\n");
                return 1;
            }
            key_len = (size_t)(eq - pair);
            if (!key_len || key_len >= sizeof(key)) {
                fprintf(stderr, "tpl: invalid --var key\n");
                return 1;
            }
            memcpy(key, pair, key_len);
            key[key_len] = 0;
            kc_tpl_var_set(&scope, key, eq + 1);
        } else {
            fprintf(stderr, "tpl: unknown option '%s'\n", argv[i]);
            return 1;
        }
        i++;
    }

    input = kc_tpl_read_fd(KC_TPL_STDIN_FD);
    if (!input) {
        fprintf(stderr, "tpl: failed to read input\n");
        kc_tpl_scope_clear(&scope);
        return 1;
    }

    kc_tpl_render(&scope, input);

    if (kc_tpl_write_fd(KC_TPL_STDOUT_FD, kc_tpl_out) != 0) {
        fprintf(stderr, "tpl: failed to write output\n");
        kc_tpl_scope_clear(&scope);
        free(input);
        return 1;
    }

    kc_tpl_scope_clear(&scope);
    free(input);
    return 0;
}
