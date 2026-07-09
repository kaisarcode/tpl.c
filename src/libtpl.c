/**
 * libtpl.c - Template renderer.
 * Summary: Core implementation for scoped template rendering.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <unistd.h>
#endif
#include <signal.h>

#include "libtpl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#endif

#define KC_TPL_EVAL_CAP 2048
#define KC_TPL_ROOT_CAP 4096
#define KC_TPL_OUTPUT_CAP 262144

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_FLOAT,
    KC_ENV_TYPE_STR
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

static const kc_env_map_t env_config_table[] = {
    { "KC_TPL_ROOT", offsetof(kc_tpl_options_t, root), KC_ENV_TYPE_STR },
    { "KC_TPL_UNTIL", offsetof(kc_tpl_options_t, until), KC_ENV_TYPE_INT },
    { "KC_TPL_CTRL", offsetof(kc_tpl_options_t, ctrl_path), KC_ENV_TYPE_STR },
};
static const int env_config_table_n = sizeof(env_config_table) / sizeof(env_config_table[0]);

typedef struct {
    int sig;
    kc_tpl_signal_callback_t cb;
} kc_tpl_signal_entry_t;

typedef struct {
    char *cmd;
    kc_tpl_ctrl_callback_t cb;
} kc_tpl_ctrl_entry_t;

typedef struct {
    int fd;
    char *buf;
    size_t used;
    size_t cap;
} kc_tpl_ctrl_conn_t;

static kc_tpl_t **g_signal_ctx_list = NULL;
static int g_signal_ctx_cap = 0;
static int g_signal_ctx_count = 0;

typedef struct {
    char key[64];
    char *val;
} kc_tpl_var_t;

typedef struct {
    char name[64];
    char body[8192];
} kc_tpl_block_t;

typedef struct kc_tpl_scope {
    kc_tpl_var_t *vars;
    int var_n;
    int var_cap;
    kc_tpl_block_t blocks[64];
    int block_n;
    struct kc_tpl_scope *parent;
} kc_tpl_scope_t;

struct kc_tpl {
    kc_tpl_options_t opts;
    char root[KC_TPL_ROOT_CAP];
    char out[KC_TPL_OUTPUT_CAP];
    size_t out_n;
    char error[256];
    kc_tpl_scope_t scope;
    kc_tpl_signal_entry_t *signal_handlers;
    int n_signal_handlers;
    int signal_handlers_capacity;
    volatile sig_atomic_t stop_requested;
    int ctrl_fd;
    char *ctrl_path;
    kc_tpl_ctrl_entry_t *ctrl_handlers;
    int n_ctrl_handlers;
    int ctrl_handlers_cap;
    kc_tpl_ctrl_conn_t *ctrl_conns;
    int n_ctrl_conns;
    int ctrl_conns_cap;
};

static const char *kc_tpl_comment_open = "{{/" "*";
static const char *kc_tpl_comment_close = "*" "/}}";

#ifndef KC_TPL_BUILD_VERSION
#define KC_TPL_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tpl_version(void) {
    return (uint64_t)KC_TPL_BUILD_VERSION;
}

/**
 * Stores the latest context error.
 * @param ctx Context pointer.
 * @param message Error summary.
 * @param detail Optional error detail.
 * @return KC_TPL_ERROR.
 */
static int kc_tpl_fail(kc_tpl_t *ctx, const char *message, const char *detail) {
    if (ctx != NULL) {
        if (detail != NULL && detail[0] != '\0') {
            snprintf(ctx->error, sizeof(ctx->error), "%s: %s", message, detail);
        } else {
            snprintf(ctx->error, sizeof(ctx->error), "%s", message);
        }
    }

    return KC_TPL_ERROR;
}

/**
 * Trims leading and trailing whitespace in place.
 * @param text Mutable string pointer.
 * @return Pointer to trimmed content.
 */
static char *kc_tpl_trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    return text;
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
    if (cap == 0U) {
        return;
    }

    if (len >= cap) {
        len = cap - 1U;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 * Appends text to the context output buffer.
 * @param ctx Context pointer.
 * @param text Source text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_put(kc_tpl_t *ctx, const char *text) {
    size_t len;

    if (ctx == NULL || text == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    len = strlen(text);
    if (ctx->out_n + len >= sizeof(ctx->out)) {
        return kc_tpl_fail(ctx, "output too large", NULL);
    }

    memcpy(ctx->out + ctx->out_n, text, len);
    ctx->out_n += len;
    ctx->out[ctx->out_n] = '\0';
    return KC_TPL_OK;
}

/**
 * Appends HTML-escaped text to the output buffer.
 * @param ctx Context pointer.
 * @param text Source text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_put_esc(kc_tpl_t *ctx, const char *text) {
    const char *cursor;

    if (text == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    for (cursor = text; *cursor != '\0'; cursor++) {
        char one[2];

        if (*cursor == '&' && kc_tpl_put(ctx, "&amp;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor == '<' && kc_tpl_put(ctx, "&lt;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor == '>' && kc_tpl_put(ctx, "&gt;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor == '"' && kc_tpl_put(ctx, "&quot;") != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (*cursor != '&' && *cursor != '<' && *cursor != '>' && *cursor != '"') {
            one[0] = *cursor;
            one[1] = '\0';
            if (kc_tpl_put(ctx, one) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        }
    }

    return KC_TPL_OK;
}

/**
 * Duplicates a null-terminated string.
 * @param text Source text.
 * @return Owned copy, or NULL on allocation failure.
 */
static char *kc_tpl_dup(const char *text) {
    size_t len;
    char *out;

    if (text == NULL) {
        return NULL;
    }

    len = strlen(text);
    out = (char *)malloc(len + 1U);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, text, len + 1U);
    return out;
}

/**
 * Resolves a variable pointer from the scope chain.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @return Value pointer or NULL when not found.
 */
static const char *kc_tpl_var_find(kc_tpl_scope_t *scope, const char *key) {
    int i;

    for (; scope != NULL; scope = scope->parent) {
        for (i = 0; i < scope->var_n; i++) {
            if (strcmp(scope->vars[i].key, key) == 0) {
                return scope->vars[i].val;
            }
        }
    }

    return NULL;
}

/**
 * Resolves a variable from the scope chain.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @return Value or empty string.
 */
static const char *kc_tpl_var_get(kc_tpl_scope_t *scope, const char *key) {
    const char *value;

    value = kc_tpl_var_find(scope, key);
    return value != NULL ? value : "";
}

/**
 * Stores or updates a variable in the current scope.
 * @param scope Scope pointer.
 * @param key Variable name.
 * @param value Variable value.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_var_set(kc_tpl_scope_t *scope, const char *key, const char *value) {
    int i;
    char *copy;

    if (
        scope == NULL ||
        key == NULL ||
        value == NULL ||
        key[0] == '\0' ||
        strlen(key) >= sizeof(scope->vars[0].key)
    ) {
        return KC_TPL_ERROR;
    }

    for (i = 0; i < scope->var_n; i++) {
        if (strcmp(scope->vars[i].key, key) == 0) {
            break;
        }
    }

    if (i == scope->var_n) {
        if (scope->var_n >= scope->var_cap) {
            int new_cap = scope->var_cap == 0 ? 16 : scope->var_cap * 2;
            kc_tpl_var_t *new_vars = realloc(
                scope->vars, (size_t)new_cap * sizeof(kc_tpl_var_t)
            );
            if (new_vars == NULL) {
                return KC_TPL_ERROR;
            }
            memset(
                new_vars + scope->var_n, 0,
                (size_t)(new_cap - scope->var_n) * sizeof(kc_tpl_var_t)
            );
            scope->vars = new_vars;
            scope->var_cap = new_cap;
        }

        scope->var_n++;
    }

    copy = kc_tpl_dup(value);
    if (copy == NULL) {
        return KC_TPL_ERROR;
    }

    snprintf(scope->vars[i].key, sizeof(scope->vars[i].key), "%s", key);
    free(scope->vars[i].val);
    scope->vars[i].val = copy;
    return KC_TPL_OK;
}

/**
 * Releases dynamic variable allocations in one scope level.
 * @param scope Scope pointer.
 * @return None.
 */
static void kc_tpl_scope_clear(kc_tpl_scope_t *scope) {
    int i;

    if (scope == NULL) {
        return;
    }

    for (i = 0; i < scope->var_n; i++) {
        free(scope->vars[i].val);
        scope->vars[i].val = NULL;
    }

    free(scope->vars);
    scope->vars = NULL;
    scope->var_n = 0;
    scope->var_cap = 0;
}

/**
 * Resolves a block body from the scope chain.
 * @param scope Scope pointer.
 * @param name Block name.
 * @return Block body or NULL when not found.
 */
static const char *kc_tpl_block_get(kc_tpl_scope_t *scope, const char *name) {
    int i;

    for (; scope != NULL; scope = scope->parent) {
        for (i = 0; i < scope->block_n; i++) {
            if (strcmp(scope->blocks[i].name, name) == 0) {
                return scope->blocks[i].body;
            }
        }
    }

    return NULL;
}

/**
 * Stores or updates a block in the current scope.
 * @param scope Scope pointer.
 * @param name Block name.
 * @param body Block body.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_block_set(kc_tpl_scope_t *scope, const char *name, const char *body) {
    int i;

    if (scope == NULL || name == NULL || body == NULL || name[0] == '\0') {
        return KC_TPL_ERROR;
    }

    for (i = 0; i < scope->block_n; i++) {
        if (strcmp(scope->blocks[i].name, name) == 0) {
            break;
        }
    }

    if (i == scope->block_n) {
        if (scope->block_n >= (int)(sizeof(scope->blocks) / sizeof(scope->blocks[0]))) {
            return KC_TPL_ERROR;
        }

        scope->block_n++;
    }

    snprintf(scope->blocks[i].name, sizeof(scope->blocks[i].name), "%s", name);
    snprintf(scope->blocks[i].body, sizeof(scope->blocks[i].body), "%s", body);
    return KC_TPL_OK;
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

    if (strchr(expr, '.') == NULL) {
        return NULL;
    }

    snprintf(path, sizeof(path), "%s", expr);
    dot = strchr(path, '.');
    if (dot == NULL || dot == path || dot[1] == '\0') {
        return NULL;
    }

    *dot = '\0';
    snprintf(left, sizeof(left), "%s", kc_tpl_trim(path));
    snprintf(right, sizeof(right), "%s", kc_tpl_trim(dot + 1));
    if (left[0] == '\0' || right[0] == '\0') {
        return NULL;
    }

    key[0] = '\0';
    strncat(key, left, sizeof(key) - strlen(key) - 1U);
    snprintf(tail, sizeof(tail), "%s", right);
    save = tail;
    while ((tok = strtok(save, ".")) != NULL) {
        save = NULL;
        strncat(key, "_", sizeof(key) - strlen(key) - 1U);
        strncat(key, kc_tpl_trim(tok), sizeof(key) - strlen(key) - 1U);
    }

    found = kc_tpl_var_find(scope, key);
    if (found != NULL) {
        return kc_tpl_dup(found);
    }

    alias = kc_tpl_var_find(scope, left);
    if (alias == NULL || alias[0] == '\0') {
        return NULL;
    }

    key[0] = '\0';
    strncat(key, alias, sizeof(key) - strlen(key) - 1U);
    snprintf(tail, sizeof(tail), "%s", right);
    save = tail;
    while ((tok = strtok(save, ".")) != NULL) {
        save = NULL;
        strncat(key, "_", sizeof(key) - strlen(key) - 1U);
        strncat(key, kc_tpl_trim(tok), sizeof(key) - strlen(key) - 1U);
    }

    found = kc_tpl_var_find(scope, key);
    return found != NULL ? kc_tpl_dup(found) : NULL;
}

/**
 * Evaluates a template expression to an owned string.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @return Owned evaluated value, or NULL on allocation failure.
 */
static char *kc_tpl_eval_dup(kc_tpl_scope_t *scope, char *expr) {
    char *text;
    char *out;
    size_t len;

    text = kc_tpl_trim(expr);
    len = strlen(text);

    if (len >= 2U && text[0] == '"' && text[len - 1U] == '"') {
        out = (char *)malloc(len - 1U);
        if (out == NULL) {
            return NULL;
        }

        memcpy(out, text + 1, len - 2U);
        out[len - 2U] = '\0';
        return out;
    }

    if (
        strcmp(text, "true") == 0 ||
        strcmp(text, "false") == 0 ||
        strcmp(text, "null") == 0
    ) {
        return kc_tpl_dup(text);
    }

    out = kc_tpl_eval_dot(scope, text);
    if (out != NULL) {
        return out;
    }

    return kc_tpl_dup(kc_tpl_var_get(scope, text));
}

/**
 * Evaluates an expression into a bounded output buffer.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @param out Destination buffer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_eval(kc_tpl_scope_t *scope, char *expr, char *out) {
    char *value;

    value = kc_tpl_eval_dup(scope, expr);
    if (value == NULL) {
        return KC_TPL_ERROR;
    }

    snprintf(out, KC_TPL_EVAL_CAP, "%s", value);
    free(value);
    return KC_TPL_OK;
}

/**
 * Evaluates truthiness of an expression.
 * @param scope Scope pointer.
 * @param expr Expression text.
 * @return 1 when truthy, or 0 otherwise.
 */
static int kc_tpl_truth(kc_tpl_scope_t *scope, char *expr) {
    char value[KC_TPL_EVAL_CAP];

    if (kc_tpl_eval(scope, expr, value) != KC_TPL_OK) {
        return 0;
    }

    return (
        value[0] != '\0' &&
        strcmp(value, "0") != 0 &&
        strcmp(value, "false") != 0 &&
        strcmp(value, "null") != 0
    );
}

/**
 * Splits a list expression into comma-separated items.
 * @param text List text.
 * @param out Output item matrix.
 * @return Number of items parsed.
 */
static int kc_tpl_list(char *text, char out[64][128]) {
    int count;
    char buf[2048];
    char *item;

    count = 0;
    snprintf(buf, sizeof(buf), "%s", kc_tpl_trim(text));
    if (buf[0] == '[' && buf[strlen(buf) - 1U] == ']') {
        memmove(buf, buf + 1, strlen(buf));
        buf[strlen(buf) - 1U] = '\0';
    }

    item = strtok(buf, ",");
    while (item != NULL && count < 64) {
        char *trimmed;
        size_t len;

        trimmed = kc_tpl_trim(item);
        len = strlen(trimmed);
        if (len >= 2U && trimmed[0] == '"' && trimmed[len - 1U] == '"') {
            trimmed[len - 1U] = '\0';
            trimmed++;
        }

        snprintf(out[count], 128, "%s", trimmed);
        count++;
        item = strtok(NULL, ",");
    }

    return count;
}

/**
 * Loads a file relative to the configured include root.
 * @param ctx Context pointer.
 * @param path Relative include path.
 * @param out Destination buffer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_load(kc_tpl_t *ctx, const char *path, char *out) {
    char full[8192];
    FILE *fp;
    size_t len;

    if (ctx == NULL || path == NULL || out == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    snprintf(full, sizeof(full), "%s/%s", ctx->root, path);
    fp = fopen(full, "rb");
    if (fp == NULL) {
        return kc_tpl_fail(ctx, "include not found", path);
    }

    len = fread(out, 1, 8191U, fp);
    if (ferror(fp)) {
        fclose(fp);
        return kc_tpl_fail(ctx, "include read failed", path);
    }

    fclose(fp);
    out[len] = '\0';
    return KC_TPL_OK;
}

/**
 * Advances past HTML comments, returning the next template tag.
 * @param tpl Template cursor.
 * @return Pointer to next tag or NULL.
 */
static char *kc_tpl_next_tag(char *tpl) {
    char *cursor;

    cursor = tpl;
    while (*cursor != '\0') {
        char *tag;
        char *comment;

        tag = strstr(cursor, "{{");
        comment = strstr(cursor, "<!--");
        if (comment == NULL) {
            return tag;
        }

        if (tag == NULL || comment < tag) {
            char *end;

            end = strstr(comment + 4, "-->");
            if (end == NULL) {
                return tag;
            }

            cursor = end + 3;
            continue;
        }

        return tag;
    }

    return NULL;
}

/**
 * Finds the matching close directive.
 * @param tpl Template text after the opening tag.
 * @param open Opening directive keyword.
 * @param close Closing directive keyword.
 * @param alt Optional alternate keyword.
 * @param mid Output pointer set to the alternate position.
 * @return Pointer to the closing tag or NULL.
 */
static char *kc_tpl_find(
    char *tpl,
    const char *open,
    const char *close,
    const char *alt,
    char **mid
) {
    int depth;
    char *cursor;

    depth = 1;
    cursor = tpl;
    *mid = NULL;

    while ((cursor = kc_tpl_next_tag(cursor)) != NULL) {
        if (strncmp(cursor, kc_tpl_comment_open, 4) == 0) {
            char *end;

            end = strstr(cursor + 4, kc_tpl_comment_close);
            if (end == NULL) {
                return NULL;
            }

            cursor = end + 4;
            continue;
        }

        if (strncmp(cursor, "{{@", 3) != 0) {
            cursor += 2;
            continue;
        }

        if (strncmp(cursor + 3, open, strlen(open)) == 0) {
            depth++;
        } else if (
            alt != NULL &&
            depth == 1 &&
            strncmp(cursor + 3, alt, strlen(alt)) == 0 &&
            *mid == NULL
        ) {
            *mid = cursor;
        } else if (strncmp(cursor + 3, close, strlen(close)) == 0) {
            depth--;
            if (depth == 0) {
                return cursor;
            }
        }

        cursor += 3;
    }

    return NULL;
}

static int kc_tpl_render_internal(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tpl);

/**
 * Parses inline block properties into a child scope.
 * @param scope Parent scope.
 * @param child Child scope.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_block_props(kc_tpl_scope_t *scope, kc_tpl_scope_t *child, char *tag) {
    char props[512];
    char *left;
    char *right;
    char open;
    char close;

    left = strchr(tag, '{');
    right = strrchr(tag, '}');
    open = '{';
    close = '}';
    if (left == NULL || right == NULL || right <= left) {
        left = strchr(tag, '[');
        right = strrchr(tag, ']');
        open = '[';
        close = ']';
    }

    if (left == NULL || right == NULL || right <= left || left[0] != open || right[0] != close) {
        return KC_TPL_OK;
    }

    snprintf(props, (size_t)(right - left), "%s", left + 1);
    for (char *item = strtok(props, ","); item != NULL; item = strtok(NULL, ",")) {
        char key[64];
        char expr[512];
        char val[KC_TPL_EVAL_CAP];
        char *name;
        char *sep;

        sep = strchr(item, ':');
        if (sep == NULL) {
            continue;
        }

        *sep = '\0';
        name = kc_tpl_trim(item);
        if (name[0] == '"') {
            size_t len;

            len = strlen(name);
            if (len >= 2U && name[len - 1U] == '"') {
                name[len - 1U] = '\0';
                name++;
            }
        }

        snprintf(key, sizeof(key), "%s", name);
        snprintf(expr, sizeof(expr), "%s", sep + 1);
        if (kc_tpl_eval(scope, expr, val) != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }

        if (kc_tpl_var_set(child, key, val) != KC_TPL_OK) {
            return KC_TPL_ERROR;
        }
    }

    return KC_TPL_OK;
}

/**
 * Renders one include directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_include(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tag) {
    char file[KC_TPL_ROOT_CAP];
    char data[8192];
    char expr[512];

    snprintf(expr, sizeof(expr), "%s", kc_tpl_trim(tag + 9));
    if (kc_tpl_eval(scope, expr, file) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
    }

    if (kc_tpl_load(ctx, file, data) != KC_TPL_OK) {
        return KC_TPL_ERROR;
    }

    return kc_tpl_render_internal(ctx, scope, data);
}

/**
 * Renders one variable assignment directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_var(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tag) {
    char name[64];
    char expr[512];
    char val[KC_TPL_EVAL_CAP];
    char *start;

    if (sscanf(tag + 5, "%63s", name) != 1) {
        return kc_tpl_fail(ctx, "invalid var directive", NULL);
    }

    start = strstr(tag + 5, name);
    if (start == NULL) {
        return kc_tpl_fail(ctx, "invalid var directive", NULL);
    }

    snprintf(expr, sizeof(expr), "%s", start + strlen(name));
    if (kc_tpl_eval(scope, expr, val) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
    }

    if (kc_tpl_var_set(scope, name, val) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "variable assignment failed", name);
    }

    return KC_TPL_OK;
}

/**
 * Renders one block directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_block(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tag) {
    char name[64];
    kc_tpl_scope_t child;
    const char *body;
    int rc;

    memset(&child, 0, sizeof(child));
    child.parent = scope;

    if (sscanf(tag + 7, "%63s", name) != 1) {
        return kc_tpl_fail(ctx, "invalid block directive", NULL);
    }

    body = kc_tpl_block_get(scope, name);
    if (body == NULL) {
        return KC_TPL_OK;
    }

    if (kc_tpl_block_props(scope, &child, tag) != KC_TPL_OK) {
        kc_tpl_scope_clear(&child);
        return kc_tpl_fail(ctx, "invalid block properties", name);
    }

    rc = kc_tpl_render_internal(ctx, &child, (char *)body);
    kc_tpl_scope_clear(&child);
    return rc;
}

/**
 * Renders one conditional directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @param body_start Pointer after opening tag.
 * @param out_end Output pointer set after closing tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_if(
    kc_tpl_t *ctx,
    kc_tpl_scope_t *scope,
    char *tag,
    char *body_start,
    char **out_end
) {
    char body[8192];
    char alt_body[8192];
    char *mid;
    char *end;

    end = kc_tpl_find(body_start, "if ", "endif", "else", &mid);
    if (end == NULL) {
        return kc_tpl_fail(ctx, "missing endif", NULL);
    }

    if (mid != NULL) {
        kc_tpl_copy(body, sizeof(body), body_start, (size_t)(mid - body_start));
        kc_tpl_copy(alt_body, sizeof(alt_body), mid + 9, (size_t)(end - (mid + 9)));
    } else {
        kc_tpl_copy(body, sizeof(body), body_start, (size_t)(end - body_start));
        alt_body[0] = '\0';
    }

    *out_end = end + 10;
    return kc_tpl_render_internal(ctx, scope, kc_tpl_truth(scope, tag + 4) ? body : alt_body);
}

/**
 * Renders one foreach directive.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tag Directive tag.
 * @param body_start Pointer after opening tag.
 * @param out_end Output pointer set after closing tag.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_foreach(
    kc_tpl_t *ctx,
    kc_tpl_scope_t *scope,
    char *tag,
    char *body_start,
    char **out_end
) {
    char item[64];
    char list_expr[256];
    char body[8192];
    char expr[512];
    char val[KC_TPL_EVAL_CAP];
    char items[64][128];
    char *in;
    char *mid;
    char *end;
    int count;
    int i;

    snprintf(expr, sizeof(expr), "%s", tag + 9);
    in = strstr(expr, " in ");
    if (in == NULL) {
        return kc_tpl_fail(ctx, "invalid foreach", NULL);
    }

    *in = '\0';
    snprintf(item, sizeof(item), "%s", kc_tpl_trim(expr));
    snprintf(list_expr, sizeof(list_expr), "%s", kc_tpl_trim(in + 4));
    if (kc_tpl_eval(scope, list_expr, val) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
    }

    count = kc_tpl_list(val, items);
    end = kc_tpl_find(body_start, "foreach ", "endforeach", NULL, &mid);
    if (end == NULL) {
        return kc_tpl_fail(ctx, "missing endforeach", NULL);
    }

    kc_tpl_copy(body, sizeof(body), body_start, (size_t)(end - body_start));
    for (i = 0; i < count; i++) {
        kc_tpl_scope_t child;
        int rc;

        memset(&child, 0, sizeof(child));
        child.parent = scope;
        if (kc_tpl_var_set(&child, item, items[i]) != KC_TPL_OK) {
            kc_tpl_scope_clear(&child);
            return kc_tpl_fail(ctx, "foreach assignment failed", item);
        }

        rc = kc_tpl_render_internal(ctx, &child, body);
        kc_tpl_scope_clear(&child);
        if (rc != KC_TPL_OK) {
            return rc;
        }
    }

    *out_end = end + 15;
    return KC_TPL_OK;
}

/**
 * Renders a template string with scope-aware directive evaluation.
 * @param ctx Context pointer.
 * @param scope Scope pointer.
 * @param tpl Template text.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
static int kc_tpl_render_internal(kc_tpl_t *ctx, kc_tpl_scope_t *scope, char *tpl) {
    char *cursor;

    cursor = tpl;
    while (*cursor != '\0') {
        char *tag_start;
        char *tag_end;
        char tag[512];

        tag_start = kc_tpl_next_tag(cursor);
        if (tag_start == NULL) {
            return kc_tpl_put(ctx, cursor);
        }

        if (tag_start > cursor) {
            char keep;

            keep = *tag_start;
            *tag_start = '\0';
            if (kc_tpl_put(ctx, cursor) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }

            *tag_start = keep;
        }

        if (strncmp(tag_start, kc_tpl_comment_open, 4) == 0) {
            tag_end = strstr(tag_start + 4, kc_tpl_comment_close);
            if (tag_end == NULL) {
                return kc_tpl_fail(ctx, "unterminated comment", NULL);
            }

            cursor = tag_end + 4;
            continue;
        }

        if (strncmp(tag_start, "{{{", 3) == 0) {
            char expr[512];
            char *value;

            tag_end = strstr(tag_start + 3, "}}}");
            if (tag_end == NULL) {
                return kc_tpl_fail(ctx, "unterminated raw tag", NULL);
            }

            kc_tpl_copy(expr, sizeof(expr), tag_start + 3, (size_t)(tag_end - (tag_start + 3)));
            value = kc_tpl_eval_dup(scope, expr);
            if (value == NULL) {
                return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
            }

            if (kc_tpl_put(ctx, value) != KC_TPL_OK) {
                free(value);
                return KC_TPL_ERROR;
            }

            free(value);
            cursor = tag_end + 3;
            continue;
        }

        tag_end = strstr(tag_start + 2, "}}");
        if (tag_end == NULL) {
            return kc_tpl_fail(ctx, "unterminated tag", NULL);
        }

        kc_tpl_copy(tag, sizeof(tag), tag_start + 2, (size_t)(tag_end - (tag_start + 2)));
        if (tag[0] != '@') {
            char *text;

            text = kc_tpl_eval_dup(scope, tag);
            if (text == NULL) {
                return kc_tpl_fail(ctx, "expression evaluation failed", NULL);
            }

            if (kc_tpl_put_esc(ctx, text) != KC_TPL_OK) {
                free(text);
                return KC_TPL_ERROR;
            }

            free(text);
            cursor = tag_end + 2;
            continue;
        }

        if (strncmp(tag + 1, "include ", 8) == 0) {
            if (kc_tpl_render_include(ctx, scope, tag) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strncmp(tag + 1, "var ", 4) == 0) {
            if (kc_tpl_render_var(ctx, scope, tag) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strncmp(tag + 1, "setblock ", 9) == 0) {
            char name[64];
            char body[8192];
            char *mid;
            char *end;

            if (sscanf(tag + 10, "%63s", name) != 1) {
                return kc_tpl_fail(ctx, "invalid setblock directive", NULL);
            }

            end = kc_tpl_find(tag_end + 2, "setblock ", "endsetblock", NULL, &mid);
            if (end == NULL) {
                return kc_tpl_fail(ctx, "missing endsetblock", NULL);
            }

            kc_tpl_copy(body, sizeof(body), tag_end + 2, (size_t)(end - (tag_end + 2)));
            if (kc_tpl_block_set(scope, name, body) != KC_TPL_OK) {
                return kc_tpl_fail(ctx, "block assignment failed", name);
            }

            cursor = end + 16;
            continue;
        } else if (strncmp(tag + 1, "block ", 6) == 0) {
            if (kc_tpl_render_block(ctx, scope, tag) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }
        } else if (strncmp(tag + 1, "if ", 3) == 0) {
            if (kc_tpl_render_if(ctx, scope, tag, tag_end + 2, &cursor) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }

            continue;
        } else if (strncmp(tag + 1, "foreach ", 8) == 0) {
            if (kc_tpl_render_foreach(ctx, scope, tag, tag_end + 2, &cursor) != KC_TPL_OK) {
                return KC_TPL_ERROR;
            }

            continue;
        }

        cursor = tag_end + 2;
    }

    return KC_TPL_OK;
}

/**
 * Initialize a renderer context with provided options.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_open(kc_tpl_t **out, const kc_tpl_options_t *opts) {
    kc_tpl_t *ctx;

    if (out == NULL || opts == NULL) {
        return KC_TPL_ERROR;
    }

    ctx = (kc_tpl_t *)calloc(1U, sizeof(kc_tpl_t));
    if (ctx == NULL) {
        return KC_TPL_ERROR;
    }

    ctx->opts = *opts;
    if (opts->root != NULL) {
        ctx->opts.root = kc_tpl_dup(opts->root);
        if (ctx->opts.root == NULL) {
            free(ctx);
            return KC_TPL_ERROR;
        }
    }
    ctx->opts.ctrl_path = opts->ctrl_path ? strdup(opts->ctrl_path) : NULL;
    if (opts->root != NULL) {
        snprintf(ctx->root, sizeof(ctx->root), "%s", opts->root);
    } else {
        snprintf(ctx->root, sizeof(ctx->root), ".");
    }
    ctx->ctrl_fd = -1;
    ctx->ctrl_path = NULL;
    ctx->ctrl_handlers = NULL;
    ctx->n_ctrl_handlers = 0;
    ctx->ctrl_handlers_cap = 0;
    ctx->ctrl_conns = NULL;
    ctx->n_ctrl_conns = 0;
    ctx->ctrl_conns_cap = 0;
    snprintf(ctx->error, sizeof(ctx->error), "ok");
    *out = ctx;
    return KC_TPL_OK;
}

/**
 * Release a renderer context and its owned data.
 * @param ctx Context pointer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_close(kc_tpl_t *ctx) {
    int i;
    if (ctx == NULL) {
        return KC_TPL_OK;
    }

    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] == ctx) {
            g_signal_ctx_list[i] = g_signal_ctx_list[--g_signal_ctx_count];
            break;
        }
    }

    kc_tpl_ctrl_close(ctx);
    kc_tpl_scope_clear(&ctx->scope);
    kc_tpl_options_free(&ctx->opts);
    free(ctx->signal_handlers);
    free(ctx->ctrl_handlers);
    free(ctx);
    return KC_TPL_OK;
}

/**
 * Sets the include root used by include directives.
 * @param ctx Context pointer.
 * @param root Include root path.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_set_root(kc_tpl_t *ctx, const char *root) {
    if (ctx == NULL || root == NULL || root[0] == '\0') {
        return kc_tpl_fail(ctx, "invalid root", NULL);
    }

    snprintf(ctx->root, sizeof(ctx->root), "%s", root);
    return KC_TPL_OK;
}

/**
 * Stores or updates one renderer variable.
 * @param ctx Context pointer.
 * @param key Variable key.
 * @param value Variable value.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_set_var(kc_tpl_t *ctx, const char *key, const char *value) {
    if (ctx == NULL) {
        return KC_TPL_ERROR;
    }

    if (kc_tpl_var_set(&ctx->scope, key, value) != KC_TPL_OK) {
        return kc_tpl_fail(ctx, "variable assignment failed", key);
    }

    return KC_TPL_OK;
}

/**
 * Renders one template string into an owned output buffer.
 * @param ctx Context pointer.
 * @param input Template input.
 * @param output Destination pointer for owned output.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_render_string(kc_tpl_t *ctx, const char *input, char **output) {
    char *work;
    char *copy;
    int rc;

    if (ctx == NULL || input == NULL || output == NULL) {
        return kc_tpl_fail(ctx, "invalid argument", NULL);
    }

    *output = NULL;
    work = kc_tpl_dup(input);
    if (work == NULL) {
        return kc_tpl_fail(ctx, "out of memory", NULL);
    }

    ctx->out_n = 0U;
    ctx->out[0] = '\0';
    rc = kc_tpl_render_internal(ctx, &ctx->scope, work);
    free(work);
    if (rc != KC_TPL_OK) {
        return rc;
    }

    copy = kc_tpl_dup(ctx->out);
    if (copy == NULL) {
        return kc_tpl_fail(ctx, "out of memory", NULL);
    }

    *output = copy;
    return KC_TPL_OK;
}

/**
 * Returns the latest context error.
 * @param ctx Context pointer.
 * @return Static or context-owned error text.
 */
const char *kc_tpl_strerror(const kc_tpl_t *ctx) {
    if (ctx == NULL) {
        return "invalid context";
    }

    return ctx->error;
}

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_tpl_options_t kc_tpl_options_default(void) {
    kc_tpl_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.until = 4;
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_tpl_options_load_env(kc_tpl_options_t *opts) {
    int i;
    if (!opts) return;
    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        char *end;
        if (!val) continue;
        switch (env_config_table[i].type) {
            case KC_ENV_TYPE_INT: {
                long v = strtol(val, &end, 10);
                if (end != val && *end == '\0') {
                    *(int *)((char *)opts + env_config_table[i].offset) = (int)v;
                }
                break;
            }
            case KC_ENV_TYPE_FLOAT: {
                float v = strtof(val, &end);
                if (end != val && *end == '\0') {
                    *(float *)((char *)opts + env_config_table[i].offset) = v;
                }
                break;
            }
            case KC_ENV_TYPE_STR: {
                char **p = (char **)((char *)opts + env_config_table[i].offset);
                free(*p);
                *p = strdup(val);
                break;
            }
        }
    }
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_tpl_options_free(kc_tpl_options_t *opts) {
    if (!opts) return;
    free(opts->root);
    opts->root = NULL;
    free(opts->ctrl_path);
    opts->ctrl_path = NULL;
}

/**
 * Request stop for a specific tpl context.
 * @param ctx Context pointer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_stop(kc_tpl_t *ctx) {
    if (!ctx) return KC_TPL_ERROR;
    ctx->stop_requested = 1;
    return KC_TPL_OK;
}

/**
 * Register a handler for a library-level signal number.
 * @param ctx Context pointer.
 * @param sig Application-defined signal number.
 * @param cb Callback to invoke.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_on_signal(kc_tpl_t *ctx, int sig, kc_tpl_signal_callback_t cb) {
    int i;
    if (!ctx) return KC_TPL_ERROR;
    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            if (cb) {
                ctx->signal_handlers[i].cb = cb;
            } else {
                int tail = ctx->n_signal_handlers - i - 1;
                if (tail > 0) {
                    memmove(&ctx->signal_handlers[i],
                            &ctx->signal_handlers[i + 1],
                            (size_t)tail * sizeof(kc_tpl_signal_entry_t));
                }
                ctx->n_signal_handlers--;
            }
            return KC_TPL_OK;
        }
    }
    if (!cb) return KC_TPL_OK;
    if (ctx->n_signal_handlers >= ctx->signal_handlers_capacity) {
        int new_cap = ctx->signal_handlers_capacity ? ctx->signal_handlers_capacity * 2 : 4;
        kc_tpl_signal_entry_t *p = (kc_tpl_signal_entry_t *)realloc(ctx->signal_handlers,
            (size_t)new_cap * sizeof(kc_tpl_signal_entry_t));
        if (!p) return KC_TPL_ERROR;
        ctx->signal_handlers = p;
        ctx->signal_handlers_capacity = new_cap;
    }
    ctx->signal_handlers[ctx->n_signal_handlers].sig = sig;
    ctx->signal_handlers[ctx->n_signal_handlers].cb = cb;
    ctx->n_signal_handlers++;
    return KC_TPL_OK;
}

/**
 * Raise a library-level signal.
 * @param ctx Context pointer.
 * @param sig Signal number to raise.
 * @return KC_TPL_OK if handled, or KC_TPL_ERROR if no handler.
 */
int kc_tpl_raise_signal(kc_tpl_t *ctx, int sig) {
    int i;
    if (!ctx) return KC_TPL_ERROR;
    for (i = 0; i < ctx->n_signal_handlers; i++) {
        if (ctx->signal_handlers[i].sig == sig) {
            ctx->signal_handlers[i].cb(ctx);
            return KC_TPL_OK;
        }
    }
    return KC_TPL_ERROR;
}

/**
 * Set the internal signal-listener context.
 * @param ctx Context pointer.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR if ctx is NULL.
 */
int kc_tpl_listen_signals(kc_tpl_t *ctx) {
    if (!ctx) return KC_TPL_ERROR;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_tpl_t **new_list = (kc_tpl_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_tpl_t *));
        if (!new_list) return KC_TPL_ERROR;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = ctx;
    return KC_TPL_OK;
}

/**
 * Wire an OS signal to the library signal listener.
 * @param ctx Context pointer.
 * @param sig_id OS signal number.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_listen_signal(kc_tpl_t *ctx, int sig_id) {
    if (!ctx) return KC_TPL_ERROR;
    if (g_signal_ctx_count >= g_signal_ctx_cap) {
        int new_cap = g_signal_ctx_cap ? g_signal_ctx_cap * 2 : 4;
        kc_tpl_t **new_list = (kc_tpl_t **)realloc(g_signal_ctx_list,
            (size_t)new_cap * sizeof(kc_tpl_t *));
        if (!new_list) return KC_TPL_ERROR;
        g_signal_ctx_list = new_list;
        g_signal_ctx_cap = new_cap;
    }
    g_signal_ctx_list[g_signal_ctx_count++] = ctx;
#ifdef _WIN32
    (void)sig_id;
#else
    signal(sig_id, kc_tpl_signal_listener);
#endif
    return KC_TPL_OK;
}

/**
 * Generic signal-listener compatible with signal() / sigaction().
 * @param sig OS signal number.
 * @return None.
 */
void kc_tpl_signal_listener(int sig) {
    int i;
    for (i = 0; i < g_signal_ctx_count; i++) {
        if (g_signal_ctx_list[i] &&
            kc_tpl_raise_signal(g_signal_ctx_list[i], sig) == 0)
            return;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

#ifndef _WIN32

/**
 * Sends a text message to a control connection.
 * @param fd File descriptor.
 * @param msg Message string.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure.
 */
static int kc_tpl_ctrl_send(int fd, const char *msg) {
    size_t len = strlen(msg);
    return (size_t)write(fd, msg, len) == len ? KC_TPL_OK : KC_TPL_ERROR;
}

/**
 * Default handler for HELP command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_TPL_OK.
 */
static int kc_tpl_ctrl_default_help(kc_tpl_t *ctx, int fd, int argc, char **argv) {
    int i;
    char tmp[4096];
    size_t pos = 0;
    (void)argc;
    (void)argv;
    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        size_t len = strlen(ctx->ctrl_handlers[i].cmd);
        if (pos + len + 2 > sizeof(tmp)) break;
        if (pos > 0) { tmp[pos] = ' '; pos++; }
        memcpy(tmp + pos, ctx->ctrl_handlers[i].cmd, len);
        pos += len;
    }
    if (pos + 1 > sizeof(tmp)) pos = sizeof(tmp) - 1;
    tmp[pos] = '\n';
    kc_tpl_ctrl_send(fd, "OK ");
    write(fd, tmp, pos + 1);
    return KC_TPL_OK;
}

/**
 * Default handler for STOP command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_TPL_OK.
 */
static int kc_tpl_ctrl_default_stop(kc_tpl_t *ctx, int fd, int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (kc_tpl_stop(ctx) == KC_TPL_OK) {
        kc_tpl_ctrl_send(fd, "OK\n");
    } else {
        kc_tpl_ctrl_send(fd, "ERR\n");
    }
    return KC_TPL_OK;
}

/**
 * Default handler for GET command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_TPL_OK.
 */
static int kc_tpl_ctrl_default_get(kc_tpl_t *ctx, int fd, int argc, char **argv) {
    char tmp[128];
    if (argc < 2) return KC_TPL_ERROR;
    if (strcmp(argv[1], "root") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %s\n", ctx->opts.root ? ctx->opts.root : "");
    } else if (strcmp(argv[1], "until") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %d\n", ctx->opts.until);
    } else if (strcmp(argv[1], "ctrl_path") == 0) {
        snprintf(tmp, sizeof(tmp), "OK %s\n", ctx->opts.ctrl_path ? ctx->opts.ctrl_path : "");
    } else {
        return KC_TPL_ERROR;
    }
    write(fd, tmp, strlen(tmp));
    return KC_TPL_OK;
}

/**
 * Default handler for SET command.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return KC_TPL_OK.
 */
static int kc_tpl_ctrl_default_set(kc_tpl_t *ctx, int fd, int argc, char **argv) {
    if (argc < 3) {
        kc_tpl_ctrl_send(fd, "ERR missing value\n");
        return KC_TPL_OK;
    }
    if (strcmp(argv[1], "root") == 0) {
        free(ctx->opts.root);
        ctx->opts.root = strdup(argv[2]);
        if (ctx->opts.root) {
            snprintf(ctx->root, sizeof(ctx->root), "%s", ctx->opts.root);
            kc_tpl_ctrl_send(fd, "OK\n");
        } else {
            kc_tpl_ctrl_send(fd, "ERR out of memory\n");
        }
        return KC_TPL_OK;
    } else if (strcmp(argv[1], "until") == 0) {
        char *end;
        long v = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || v < 0 || v > 255) {
            kc_tpl_ctrl_send(fd, "ERR invalid value\n");
            return KC_TPL_OK;
        }
        ctx->opts.until = (int)v;
        kc_tpl_ctrl_send(fd, "OK\n");
        return KC_TPL_OK;
    }
    kc_tpl_ctrl_send(fd, "ERR unknown key\n");
    return KC_TPL_OK;
}

/**
 * Parses one command line and dispatches to the registered handler.
 * @param ctx Context handle.
 * @param fd Control connection fd.
 * @param line Complete command line.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure.
 */
static int kc_tpl_ctrl_dispatch(kc_tpl_t *ctx, int fd, const char *line) {
    char *copy = NULL;
    char *argv[64];
    int argc = 0;
    int i;

    copy = strdup(line);
    if (!copy) return KC_TPL_ERROR;

    argv[argc] = strtok(copy, " \t\r\n");
    if (argv[argc]) {
        argc++;
        while (argc < 64 && (argv[argc] = strtok(NULL, " \t\r\n")) != NULL) {
            argc++;
        }
    }

    if (argc == 0) {
        free(copy);
        return KC_TPL_OK;
    }

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, argv[0]) == 0) {
            ctx->ctrl_handlers[i].cb(ctx, fd, argc, argv);
            free(copy);
            return KC_TPL_OK;
        }
    }

    kc_tpl_ctrl_send(fd, "ERR unknown command\n");
    free(copy);
    return KC_TPL_OK;
}

#endif

/**
 * Register a control command handler.
 * @param ctx Context handle.
 * @param cmd Command name.
 * @param cb Callback or NULL to remove.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure.
 */
int kc_tpl_ctrl_on(kc_tpl_t *ctx, const char *cmd, kc_tpl_ctrl_callback_t cb) {
    int i;

    if (!ctx || !cmd) return KC_TPL_ERROR;

    for (i = 0; i < ctx->n_ctrl_handlers; i++) {
        if (strcmp(ctx->ctrl_handlers[i].cmd, cmd) == 0) {
            if (cb) {
                ctx->ctrl_handlers[i].cb = cb;
            } else {
                int tail = ctx->n_ctrl_handlers - i - 1;
                if (tail > 0)
                    memmove(&ctx->ctrl_handlers[i],
                            &ctx->ctrl_handlers[i + 1],
                            (size_t)tail * sizeof(kc_tpl_ctrl_entry_t));
                ctx->n_ctrl_handlers--;
            }
            return KC_TPL_OK;
        }
    }
    if (!cb) return KC_TPL_OK;
    if (ctx->n_ctrl_handlers >= ctx->ctrl_handlers_cap) {
        int new_cap = ctx->ctrl_handlers_cap ? ctx->ctrl_handlers_cap * 2 : 4;
        kc_tpl_ctrl_entry_t *p = (kc_tpl_ctrl_entry_t *)realloc(ctx->ctrl_handlers,
            (size_t)new_cap * sizeof(kc_tpl_ctrl_entry_t));
        if (!p) return KC_TPL_ERROR;
        ctx->ctrl_handlers = p;
        ctx->ctrl_handlers_cap = new_cap;
    }
    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd = strdup(cmd);
    if (!ctx->ctrl_handlers[ctx->n_ctrl_handlers].cmd) return KC_TPL_ERROR;
    ctx->ctrl_handlers[ctx->n_ctrl_handlers].cb = cb;
    ctx->n_ctrl_handlers++;
    return KC_TPL_OK;
}

/**
 * Remove a control command handler.
 * @param ctx Context handle.
 * @param cmd Command name.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure.
 */
int kc_tpl_ctrl_off(kc_tpl_t *ctx, const char *cmd) {
    return kc_tpl_ctrl_on(ctx, cmd, NULL);
}

/**
 * Open a Unix domain socket for control commands.
 * @param ctx Context handle.
 * @param path Socket path.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure.
 */
int kc_tpl_ctrl_open(kc_tpl_t *ctx, const char *path) {
#ifndef _WIN32
    struct sockaddr_un addr;
    int fd;
    int flags;

    if (!ctx || !path) return KC_TPL_ERROR;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return KC_TPL_ERROR;

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return KC_TPL_ERROR;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        unlink(path);
        return KC_TPL_ERROR;
    }

    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    ctx->ctrl_fd = fd;
    ctx->ctrl_path = strdup(path);
    if (!ctx->ctrl_path) {
        close(fd);
        unlink(path);
        ctx->ctrl_fd = -1;
        return KC_TPL_ERROR;
    }

    kc_tpl_ctrl_on(ctx, "HELP", kc_tpl_ctrl_default_help);
    kc_tpl_ctrl_on(ctx, "STOP", kc_tpl_ctrl_default_stop);
    kc_tpl_ctrl_on(ctx, "GET", kc_tpl_ctrl_default_get);
    kc_tpl_ctrl_on(ctx, "SET", kc_tpl_ctrl_default_set);

    return KC_TPL_OK;
#else
    (void)ctx;
    (void)path;
    return KC_TPL_ERROR;
#endif
}

/**
 * Close the control socket and all active connections.
 * @param ctx Context handle.
 * @return KC_TPL_OK on success, KC_TPL_ERROR on failure.
 */
int kc_tpl_ctrl_close(kc_tpl_t *ctx) {
#ifndef _WIN32
    int i;

    if (!ctx) return KC_TPL_OK;

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            close(ctx->ctrl_conns[i].fd);
        }
        free(ctx->ctrl_conns[i].buf);
    }
    ctx->n_ctrl_conns = 0;

    if (ctx->ctrl_fd >= 0) {
        close(ctx->ctrl_fd);
        ctx->ctrl_fd = -1;
    }

    if (ctx->ctrl_path) {
        unlink(ctx->ctrl_path);
        free(ctx->ctrl_path);
        ctx->ctrl_path = NULL;
    }

    return KC_TPL_OK;
#else
    (void)ctx;
    return KC_TPL_OK;
#endif
}

/**
 * Non-blocking poll: accept connections, read and dispatch commands.
 * @param ctx Context handle.
 * @return Number of commands handled, or KC_TPL_ERROR on failure.
 */
int kc_tpl_ctrl_poll(kc_tpl_t *ctx) {
#ifndef _WIN32
    struct pollfd pfds[64];
    int nfds;
    int i;
    int handled = 0;

    if (!ctx || ctx->ctrl_fd < 0) return 0;

    nfds = 0;
    pfds[nfds].fd = ctx->ctrl_fd;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    nfds++;

    for (i = 0; i < ctx->n_ctrl_conns && nfds < 64; i++) {
        if (ctx->ctrl_conns[i].fd >= 0) {
            pfds[nfds].fd = ctx->ctrl_conns[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
    }

    if (poll(pfds, (nfds_t)nfds, 0) < 0) return KC_TPL_ERROR;

    if (pfds[0].revents & POLLIN) {
        int conn_fd = accept(ctx->ctrl_fd, NULL, NULL);
        if (conn_fd >= 0) {
            int flags = fcntl(conn_fd, F_GETFL, 0);
            fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK);

            if (ctx->n_ctrl_conns >= ctx->ctrl_conns_cap) {
                int new_cap = ctx->ctrl_conns_cap ? ctx->ctrl_conns_cap * 2 : 4;
                kc_tpl_ctrl_conn_t *p = (kc_tpl_ctrl_conn_t *)realloc(ctx->ctrl_conns,
                    (size_t)new_cap * sizeof(kc_tpl_ctrl_conn_t));
                if (p) {
                    ctx->ctrl_conns = p;
                    ctx->ctrl_conns_cap = new_cap;
                }
            }

            if (ctx->n_ctrl_conns < ctx->ctrl_conns_cap) {
                ctx->ctrl_conns[ctx->n_ctrl_conns].fd = conn_fd;
                ctx->ctrl_conns[ctx->n_ctrl_conns].buf = NULL;
                ctx->ctrl_conns[ctx->n_ctrl_conns].used = 0;
                ctx->ctrl_conns[ctx->n_ctrl_conns].cap = 0;
                ctx->n_ctrl_conns++;
            } else {
                close(conn_fd);
            }
        }
    }

    for (i = 0; i < ctx->n_ctrl_conns; i++) {
        int pidx = -1;
        int j;
        for (j = 1; j < nfds; j++) {
            if (pfds[j].fd == ctx->ctrl_conns[i].fd) {
                pidx = j;
                break;
            }
        }
        if (pidx < 0 || !(pfds[pidx].revents & POLLIN)) continue;

        for (;;) {
            char chunk[256];
            ssize_t n = read(ctx->ctrl_conns[i].fd, chunk, sizeof(chunk));
            if (n < 0) break;
            if (n == 0) {
                close(ctx->ctrl_conns[i].fd);
                ctx->ctrl_conns[i].fd = -1;
                free(ctx->ctrl_conns[i].buf);
                ctx->ctrl_conns[i].buf = NULL;
                ctx->ctrl_conns[i].used = 0;
                ctx->ctrl_conns[i].cap = 0;
                break;
            }

            size_t offset = 0;
            while ((size_t)n > offset) {
                char *nl = (char *)memchr(chunk + offset, '\n', (size_t)n - offset);
                if (!nl) {
                    size_t avail = (size_t)n - offset;
                    if (ctx->ctrl_conns[i].used + avail + 1 > ctx->ctrl_conns[i].cap) {
                        size_t new_cap = ctx->ctrl_conns[i].cap ? ctx->ctrl_conns[i].cap * 2 : 256;
                        while (new_cap < ctx->ctrl_conns[i].used + avail + 1) new_cap *= 2;
                        char *p = (char *)realloc(ctx->ctrl_conns[i].buf, new_cap);
                        if (!p) break;
                        ctx->ctrl_conns[i].buf = p;
                        ctx->ctrl_conns[i].cap = new_cap;
                    }
                    memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used, chunk + offset, avail);
                    ctx->ctrl_conns[i].used += avail;
                    ctx->ctrl_conns[i].buf[ctx->ctrl_conns[i].used] = '\0';
                    offset = (size_t)n;
                } else {
                    size_t line_len = (size_t)(nl - (chunk + offset));
                    size_t total = ctx->ctrl_conns[i].used + line_len;

                    if (total + 1 > ctx->ctrl_conns[i].cap) {
                        size_t new_cap = ctx->ctrl_conns[i].cap ? ctx->ctrl_conns[i].cap * 2 : 256;
                        while (new_cap < total + 1) new_cap *= 2;
                        char *p = (char *)realloc(ctx->ctrl_conns[i].buf, new_cap);
                        if (!p) break;
                        ctx->ctrl_conns[i].buf = p;
                        ctx->ctrl_conns[i].cap = new_cap;
                    }

                    if (line_len > 0)
                        memcpy(ctx->ctrl_conns[i].buf + ctx->ctrl_conns[i].used, chunk + offset, line_len);
                    ctx->ctrl_conns[i].buf[total] = '\0';

                    kc_tpl_ctrl_dispatch(ctx, ctx->ctrl_conns[i].fd, ctx->ctrl_conns[i].buf);
                    handled++;

                    ctx->ctrl_conns[i].used = 0;
                    offset += line_len + 1;
                }
            }
        }
    }

    {
        int write_idx = 0;
        for (i = 0; i < ctx->n_ctrl_conns; i++) {
            if (ctx->ctrl_conns[i].fd >= 0) {
                if (write_idx != i)
                    ctx->ctrl_conns[write_idx] = ctx->ctrl_conns[i];
                write_idx++;
            }
        }
        ctx->n_ctrl_conns = write_idx;
    }

    return handled;
#else
    (void)ctx;
    return 0;
#endif
}
