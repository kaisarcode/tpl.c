/**
 * test.c - libtpl portable contract tests.
 * Summary: Validates exported libtpl behavior through the public C API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tpl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int signal_count = 0;
static int signal_count_b = 0;
static kc_tpl_t *signal_ctx_seen = NULL;

/**
 * Counts primary signal callbacks.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal(kc_tpl_t *ctx) {
    if (ctx != NULL) {
        signal_count++;
        signal_ctx_seen = ctx;
    }
}

/**
 * Counts secondary signal callbacks.
 * @param ctx Context passed by the library.
 * @return None.
 */
static void count_signal_b(kc_tpl_t *ctx) {
    if (ctx != NULL) {
        signal_count_b++;
        signal_ctx_seen = ctx;
    }
}

/**
 * Sets or clears one environment variable.
 * @param name Variable name.
 * @param value Variable value, or NULL to clear.
 * @return 0 on success, 1 on failure.
 */
static int set_env_value(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value != NULL ? value : "") == 0 ? 0 : 1;
#else
    if (value == NULL) return unsetenv(name) == 0 ? 0 : 1;
    return setenv(name, value, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Verifies an integer result.
 * @param name Check name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies a true condition.
 * @param name Check name.
 * @param condition Condition expected to be true.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        fprintf(stderr, "%s: expected true, got false\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies a string result.
 * @param name Check name.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Renders one template and compares its output.
 * @param ctx Template context.
 * @param input Template text.
 * @param expected Expected rendered text.
 * @return 0 on success, 1 on failure.
 */
static int render_expect(kc_tpl_t *ctx, const char *input, const char *expected) {
    char *output;
    int rc;

    output = NULL;
    rc = kc_tpl_render_string(ctx, input, &output);
    if (rc != KC_TPL_OK) {
        fprintf(stderr, "render: expected KC_TPL_OK, got %d: %s\n", rc,
            kc_tpl_strerror(ctx));
        free(output);
        return 1;
    }

    rc = expect_string("render output", expected, output);
    free(output);
    return rc;
}

/**
 * Writes text content to a file path.
 * @param path Output path.
 * @param text File content.
 * @return 0 on success, 1 on failure.
 */
static int write_text_file(const char *path, const char *text) {
    FILE *fp;

    fp = fopen(path, "wb");
    if (fp == NULL) return 1;
    if (fputs(text, fp) == EOF) {
        fclose(fp);
        return 1;
    }
    return fclose(fp) == 0 ? 0 : 1;
}

/**
 * Verifies option defaults, environment loading, and cleanup.
 * @return 0 on success, 1 on failure.
 */
static int case_options(void) {
    kc_tpl_options_t opts;
    int rc;

    opts = kc_tpl_options_default();
    rc = 0;
    rc += expect_true("default root", opts.root == NULL);
    rc += expect_int("set root env", 0, set_env_value("KC_TPL_ROOT", "env-root"));
    kc_tpl_options_load_env(&opts);
    rc += expect_string("env root", "env-root", opts.root);
    rc += expect_int("replace root env", 0, set_env_value("KC_TPL_ROOT", "env-root-2"));
    kc_tpl_options_load_env(&opts);
    rc += expect_string("replaced env root", "env-root-2", opts.root);
    kc_tpl_options_load_env(NULL);
    kc_tpl_options_free(&opts);
    rc += expect_true("free clears root", opts.root == NULL);
    kc_tpl_options_free(NULL);
    set_env_value("KC_TPL_ROOT", NULL);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies context lifecycle, NULL guards, stop, and version.
 * @return 0 on success, 1 on failure.
 */
static int case_context(void) {
    kc_tpl_options_t opts;
    kc_tpl_t *ctx;
    kc_tpl_t *second;
    char *output;
    int rc;

    opts = kc_tpl_options_default();
    ctx = NULL;
    second = NULL;
    output = NULL;
    rc = 0;
    rc += expect_int("close NULL", KC_TPL_OK, kc_tpl_close(NULL));
    rc += expect_int("stop NULL", KC_TPL_ERROR, kc_tpl_stop(NULL));
    rc += expect_int("open NULL out", KC_TPL_ERROR, kc_tpl_open(NULL, &opts));
    rc += expect_int("open NULL opts", KC_TPL_ERROR, kc_tpl_open(&ctx, NULL));
    rc += expect_int("render NULL ctx", KC_TPL_ERROR,
        kc_tpl_render_string(NULL, "x", &output));
    rc += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx, &opts));
    rc += expect_string("initial strerror", "ok", kc_tpl_strerror(ctx));
    rc += expect_string("NULL strerror", "invalid context", kc_tpl_strerror(NULL));
    rc += expect_int("render NULL input", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, NULL, &output));
    rc += expect_int("render NULL output", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "x", NULL));
    rc += expect_int("set root NULL ctx", KC_TPL_ERROR, kc_tpl_set_root(NULL, "."));
    rc += expect_int("set root NULL value", KC_TPL_ERROR, kc_tpl_set_root(ctx, NULL));
    rc += expect_int("set root empty", KC_TPL_ERROR, kc_tpl_set_root(ctx, ""));
    rc += expect_int("set root valid", KC_TPL_OK, kc_tpl_set_root(ctx, "."));
    rc += expect_int("set var NULL ctx", KC_TPL_ERROR,
        kc_tpl_set_var(NULL, "k", "v"));
    rc += expect_int("set var empty key", KC_TPL_ERROR,
        kc_tpl_set_var(ctx, "", "v"));
    rc += expect_int("set var NULL key", KC_TPL_ERROR,
        kc_tpl_set_var(ctx, NULL, "v"));
    rc += expect_int("set var NULL value", KC_TPL_ERROR,
        kc_tpl_set_var(ctx, "k", NULL));
    rc += expect_int("open second context", KC_TPL_OK, kc_tpl_open(&second, &opts));
    rc += expect_int("stop first", KC_TPL_OK, kc_tpl_stop(ctx));
    rc += expect_int("second set var after first stop", KC_TPL_OK,
        kc_tpl_set_var(second, "x", "42"));
    rc += render_expect(second, "{{ x }}", "42");
    rc += expect_true("version set", kc_tpl_version() != 0U);
    kc_tpl_close(second);
    kc_tpl_close(ctx);
    kc_tpl_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies signal registration, replacement, removal, and listener dispatch.
 * @return 0 on success, 1 on failure.
 */
static int case_signals(void) {
    kc_tpl_options_t opts;
    kc_tpl_t *first;
    kc_tpl_t *second;
    int rc;
    int i;

    opts = kc_tpl_options_default();
    first = NULL;
    second = NULL;
    rc = 0;
    if (kc_tpl_open(&first, &opts) != KC_TPL_OK) return 1;
    if (kc_tpl_open(&second, &opts) != KC_TPL_OK) {
        kc_tpl_close(first);
        return 1;
    }
    signal_count = 0;
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    rc += expect_int("on signal NULL", KC_TPL_ERROR,
        kc_tpl_on_signal(NULL, 10, count_signal));
    rc += expect_int("raise signal NULL", KC_TPL_ERROR,
        kc_tpl_raise_signal(NULL, 10));
    rc += expect_int("listen signals NULL", KC_TPL_ERROR,
        kc_tpl_listen_signals(NULL));
    rc += expect_int("listen signal NULL", KC_TPL_ERROR,
        kc_tpl_listen_signal(NULL, 10));
    rc += expect_int("raise unhandled", KC_TPL_ERROR,
        kc_tpl_raise_signal(first, 10));
    rc += expect_int("register handler", KC_TPL_OK,
        kc_tpl_on_signal(first, 10, count_signal));
    rc += expect_int("raise handled", KC_TPL_OK,
        kc_tpl_raise_signal(first, 10));
    rc += expect_int("callback count", 1, signal_count);
    rc += expect_true("callback saw first", signal_ctx_seen == first);
    rc += expect_int("replace handler", KC_TPL_OK,
        kc_tpl_on_signal(first, 10, count_signal_b));
    signal_count = 0;
    signal_count_b = 0;
    rc += expect_int("raise replaced", KC_TPL_OK,
        kc_tpl_raise_signal(first, 10));
    rc += expect_int("old handler skipped", 0, signal_count);
    rc += expect_int("new handler called", 1, signal_count_b);
    rc += expect_int("remove handler", KC_TPL_OK,
        kc_tpl_on_signal(first, 10, NULL));
    rc += expect_int("remove missing handler", KC_TPL_OK,
        kc_tpl_on_signal(first, 10, NULL));
    rc += expect_int("raise removed", KC_TPL_ERROR,
        kc_tpl_raise_signal(first, 10));
    for (i = 0; i < 8; i++) {
        rc += expect_int("register growth handler", KC_TPL_OK,
            kc_tpl_on_signal(first, 100 + i, count_signal));
    }
    signal_count = 0;
    rc += expect_int("raise growth handler", KC_TPL_OK,
        kc_tpl_raise_signal(first, 107));
    rc += expect_int("growth callback count", 1, signal_count);
    rc += expect_int("listen first", KC_TPL_OK, kc_tpl_listen_signals(first));
    rc += expect_int("listen one signal", KC_TPL_OK, kc_tpl_listen_signal(first, 12));
    rc += expect_int("listen second", KC_TPL_OK, kc_tpl_listen_signals(second));
    rc += expect_int("second handler", KC_TPL_OK,
        kc_tpl_on_signal(second, 12, count_signal_b));
    signal_count_b = 0;
    signal_ctx_seen = NULL;
    kc_tpl_signal_listener(12);
    rc += expect_int("listener dispatch count", 1, signal_count_b);
    rc += expect_true("listener saw second", signal_ctx_seen == second);
    kc_tpl_close(second);
    kc_tpl_close(first);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies core template rendering directives.
 * @return 0 on success, 1 on failure.
 */
static int case_render_core(void) {
    kc_tpl_options_t opts;
    kc_tpl_t *ctx;
    int rc;

    opts = kc_tpl_options_default();
    ctx = NULL;
    rc = 0;
    if (kc_tpl_open(&ctx, &opts) != KC_TPL_OK) return 1;
    rc += expect_int("set title", KC_TPL_OK, kc_tpl_set_var(ctx, "title", "A&B"));
    rc += expect_int("set raw", KC_TPL_OK, kc_tpl_set_var(ctx, "raw", "<b>x</b>"));
    rc += expect_int("set items", KC_TPL_OK,
        kc_tpl_set_var(ctx, "items", "[item_1,item_2]"));
    rc += expect_int("set item one", KC_TPL_OK,
        kc_tpl_set_var(ctx, "item_1_title", "One"));
    rc += expect_int("set item two", KC_TPL_OK,
        kc_tpl_set_var(ctx, "item_2_title", "Two"));
    rc += render_expect(ctx, "<h1>{{ title }}</h1>", "<h1>A&amp;B</h1>");
    rc += render_expect(ctx, "{{{ raw }}}", "<b>x</b>");
    rc += render_expect(ctx, "{{@if title}}yes{{@else}}no{{@endif}}", "yes");
    rc += render_expect(ctx, "{{@if missing}}yes{{@else}}no{{@endif}}", "no");
    rc += render_expect(ctx,
        "{{@foreach item in items}}<b>{{ item.title }}</b>{{@endforeach}}",
        "<b>One</b><b>Two</b>");
    rc += render_expect(ctx,
        "{{@setblock card}}<i>{{ name }}</i>{{@endsetblock}}"
        "{{@block card [ \"name\": \"Ada\" ]}}",
        "<i>Ada</i>");
    rc += render_expect(ctx, "A{{/* hidden */}}B", "AB");
    rc += render_expect(ctx, "<div><!-- {{@if title}}x{{@endif}} --></div>",
        "<div><!-- {{@if title}}x{{@endif}} --></div>");
    kc_tpl_close(ctx);
    kc_tpl_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies include resolution through environment-loaded root options.
 * @return 0 on success, 1 on failure.
 */
static int case_include(void) {
    kc_tpl_options_t opts;
    kc_tpl_t *ctx;
    int rc;

    opts = kc_tpl_options_default();
    ctx = NULL;
    rc = 0;
    if (write_text_file("partial.html", "{{@var value \"<ok>\"}}{{ value }}\n") != 0) {
        return 1;
    }
    if (write_text_file("page.html",
        "<title>{{ page }}</title>\n<section>{{@include \"partial.html\"}}</section>\n") != 0) {
        remove("partial.html");
        return 1;
    }
    rc += expect_int("set env root", 0, set_env_value("KC_TPL_ROOT", "."));
    kc_tpl_options_load_env(&opts);
    rc += expect_int("open", KC_TPL_OK, kc_tpl_open(&ctx, &opts));
    if (ctx != NULL) {
        rc += expect_int("set page", KC_TPL_OK, kc_tpl_set_var(ctx, "page", "Home"));
        rc += render_expect(ctx, "{{@include \"page.html\"}}",
            "<title>Home</title>\n<section>&lt;ok&gt;\n</section>\n");
        kc_tpl_close(ctx);
    }
    kc_tpl_options_free(&opts);
    set_env_value("KC_TPL_ROOT", NULL);
    remove("page.html");
    remove("partial.html");
    return rc == 0 ? 0 : 1;
}

/**
 * Verifies invalid templates and missing resources fail clearly.
 * @return 0 on success, 1 on failure.
 */
static int case_failure_paths(void) {
    kc_tpl_options_t opts;
    kc_tpl_t *ctx;
    char *output;
    int rc;

    opts = kc_tpl_options_default();
    ctx = NULL;
    output = NULL;
    rc = 0;
    if (kc_tpl_open(&ctx, &opts) != KC_TPL_OK) return 1;
    rc += expect_int("missing include", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{@include \"missing.html\"}}", &output));
    free(output);
    output = NULL;
    rc += expect_int("unterminated tag", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{ title", &output));
    free(output);
    output = NULL;
    rc += expect_int("unterminated raw", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{{ title", &output));
    free(output);
    output = NULL;
    rc += expect_int("unterminated comment", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{/* title", &output));
    free(output);
    output = NULL;
    rc += expect_int("missing endif", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{@if title}}x", &output));
    free(output);
    output = NULL;
    rc += expect_int("invalid foreach", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{@foreach item items}}x{{@endforeach}}", &output));
    free(output);
    output = NULL;
    rc += expect_int("missing endforeach", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{@foreach item in items}}x", &output));
    free(output);
    kc_tpl_close(ctx);
    kc_tpl_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "options") == 0) return case_options();
    if (strcmp(argv[1], "context") == 0) return case_context();
    if (strcmp(argv[1], "signals") == 0) return case_signals();
    if (strcmp(argv[1], "render-core") == 0) return case_render_core();
    if (strcmp(argv[1], "include") == 0) return case_include();
    if (strcmp(argv[1], "failure-paths") == 0) return case_failure_paths();
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
