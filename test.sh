#!/bin/sh
# Summary: Validation suite for libtpl behavior and tpl runtime integration.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

PASS=0
FAIL=0
TMP_ROOT=

# Prints one failure line.
# @param $1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    FAIL=$((FAIL + 1))
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    return 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
kc_test_pass() {
    PASS=$((PASS + 1))
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
    return 0
}

# Detects the artifact architecture for the current machine.
# @return Architecture name on stdout.
kc_test_arch() {
    case "$(uname -m)" in
        x86_64 | amd64)
            printf '%s\n' "x86_64"
            ;;
        aarch64 | arm64)
            printf '%s\n' "aarch64"
            ;;
        armv7l | armv7)
            printf '%s\n' "armv7"
            ;;
        i386 | i486 | i586 | i686)
            printf '%s\n' "i686"
            ;;
        ppc64le | powerpc64le)
            printf '%s\n' "powerpc64le"
            ;;
        *)
            uname -m
            ;;
    esac
}

# Detects the artifact platform for the current machine.
# @return Platform name on stdout.
kc_test_platform() {
    case "$(uname -s)" in
        Linux)
            printf '%s\n' "linux"
            ;;
        *)
            uname -s | tr '[:upper:]' '[:lower:]'
            ;;
    esac
}

# Returns the CLI path for the current architecture and platform.
# @return CLI path on stdout.
kc_test_binary_path() {
    printf './bin/%s/%s/tpl\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Returns the static library path for the current architecture and platform.
# @return Static library path on stdout.
kc_test_static_library_path() {
    printf './bin/%s/%s/libtpl.a\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Returns the shared library path for the current architecture and platform.
# @return Shared library path on stdout.
kc_test_shared_library_path() {
    printf './bin/%s/%s/libtpl.so\n' "$(kc_test_arch)" "$(kc_test_platform)"
}

# Removes temporary files owned by the suite.
# @return 0 on success.
kc_test_cleanup() {
    if [ -n "$TMP_ROOT" ]; then
        rm -rf "$TMP_ROOT"
    fi
    return 0
}

# Verifies the binary and library artifacts needed by behavior tests exist.
# @return 0 on success, 1 on failure.
kc_test_check_artifacts() {
    if [ ! -x "$BIN" ]; then
        kc_test_fail "runtime artifact: expected executable at $BIN, got missing"
        return 1
    fi

    if [ ! -f "$STATIC_LIB" ]; then
        kc_test_fail "static library artifact: expected file at $STATIC_LIB, got missing"
        return 1
    fi

    if [ ! -f "$SHARED_LIB" ]; then
        kc_test_fail "shared library artifact: expected file at $SHARED_LIB, got missing"
        return 1
    fi

    kc_test_pass "artifacts: runtime and libraries are available"
    return 0
}

# Writes the helper source that validates exported libtpl contracts.
# @param $1 Destination source file path.
# @return 0 on success.
kc_test_write_helper() {
    cat > "$1" <<'KC_TPL_HELPER'
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }

    return 0;
}

static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n", name, expected, actual != NULL ? actual : "NULL");
        return 1;
    }

    return 0;
}

static int render_expect(kc_tpl_t *ctx, const char *input, const char *expected) {
    char *output = NULL;
    int rc;

    rc = kc_tpl_render_string(ctx, input, &output);
    if (rc != KC_TPL_OK) {
        fprintf(stderr, "render: expected KC_TPL_OK, got %d: %s\n", rc, kc_tpl_strerror(ctx));
        free(output);
        return 1;
    }

    rc = expect_string("render output", expected, output);
    free(output);
    return rc;
}

static int case_version(void) {
    if (kc_tpl_version() == 0U) {
        fprintf(stderr, "version: expected non-zero build timestamp, got 0\n");
        return 1;
    }

    return 0;
}

static int case_lifecycle(void) {
    kc_tpl_options_t opts = kc_tpl_options_default();
    kc_tpl_t *ctx = NULL;
    char *output = NULL;
    int rc = 0;

    rc += expect_int("open(NULL, opts)", KC_TPL_ERROR, kc_tpl_open(NULL, &opts));
    rc += expect_int("open(out, NULL)", KC_TPL_ERROR, kc_tpl_open(&ctx, NULL));
    rc += expect_int("render(NULL, input, output)", KC_TPL_ERROR, kc_tpl_render_string(NULL, "x", &output));
    rc += expect_int("set_root(NULL, root)", KC_TPL_ERROR, kc_tpl_set_root(NULL, "."));
    rc += expect_int("set_var(NULL, key, value)", KC_TPL_ERROR, kc_tpl_set_var(NULL, "k", "v"));
    rc += expect_int("close(NULL)", KC_TPL_OK, kc_tpl_close(NULL));
    rc += expect_int("open(out, opts)", KC_TPL_OK, kc_tpl_open(&ctx, &opts));
    rc += expect_int("close(ctx)", KC_TPL_OK, kc_tpl_close(ctx));
    kc_tpl_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

static int case_render_core(void) {
    kc_tpl_options_t opts = kc_tpl_options_default();
    kc_tpl_t *ctx = NULL;
    int rc = 0;

    if (kc_tpl_open(&ctx, &opts) != KC_TPL_OK) {
        fprintf(stderr, "open: expected KC_TPL_OK, got KC_TPL_ERROR\n");
        return 1;
    }

    rc += expect_int("set_var title", KC_TPL_OK, kc_tpl_set_var(ctx, "title", "A&B"));
    rc += expect_int("set_var raw", KC_TPL_OK, kc_tpl_set_var(ctx, "raw", "<b>x</b>"));
    rc += expect_int("set_var items", KC_TPL_OK, kc_tpl_set_var(ctx, "items", "[item_1,item_2]"));
    rc += expect_int("set_var item_1_title", KC_TPL_OK, kc_tpl_set_var(ctx, "item_1_title", "One"));
    rc += expect_int("set_var item_2_title", KC_TPL_OK, kc_tpl_set_var(ctx, "item_2_title", "Two"));
    rc += render_expect(ctx, "<h1>{{ title }}</h1>", "<h1>A&amp;B</h1>");
    rc += render_expect(ctx, "{{{ raw }}}", "<b>x</b>");
    rc += render_expect(ctx, "{{@if title}}yes{{@else}}no{{@endif}}", "yes");
    rc += render_expect(ctx, "{{@foreach item in items}}<b>{{ item.title }}</b>{{@endforeach}}", "<b>One</b><b>Two</b>");
    rc += render_expect(ctx, "{{@setblock card}}<i>{{ name }}</i>{{@endsetblock}}{{@block card [ \"name\": \"Ada\" ]}}", "<i>Ada</i>");
    rc += render_expect(ctx, "A{{/* hidden */}}B", "AB");
    rc += render_expect(ctx, "<div><!-- {{@if title}}x{{@endif}} --></div>", "<div><!-- {{@if title}}x{{@endif}} --></div>");
    rc += expect_int("close(ctx)", KC_TPL_OK, kc_tpl_close(ctx));
    kc_tpl_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

static int case_include_env(const char *root) {
    kc_tpl_options_t opts = kc_tpl_options_default();
    kc_tpl_t *ctx = NULL;
    int rc = 0;

    if (setenv("KC_TPL_ROOT", root, 1) != 0) {
        fprintf(stderr, "setenv KC_TPL_ROOT: expected success, got failure\n");
        return 1;
    }

    kc_tpl_options_load_env(&opts);
    if (kc_tpl_open(&ctx, &opts) != KC_TPL_OK) {
        fprintf(stderr, "open env root: expected KC_TPL_OK, got KC_TPL_ERROR\n");
        kc_tpl_options_free(&opts);
        return 1;
    }

    rc += expect_int("set_var page", KC_TPL_OK, kc_tpl_set_var(ctx, "page", "Home"));
    rc += render_expect(ctx, "{{@include \"page.html\"}}", "<title>Home</title>\n<section>&lt;ok&gt;\n</section>\n");
    rc += expect_int("close(ctx)", KC_TPL_OK, kc_tpl_close(ctx));
    kc_tpl_options_free(&opts);
    unsetenv("KC_TPL_ROOT");
    return rc == 0 ? 0 : 1;
}

static int case_failure_paths(void) {
    kc_tpl_options_t opts = kc_tpl_options_default();
    kc_tpl_t *ctx = NULL;
    char *output = NULL;
    int rc = 0;

    if (kc_tpl_open(&ctx, &opts) != KC_TPL_OK) {
        fprintf(stderr, "open failure case: expected KC_TPL_OK, got KC_TPL_ERROR\n");
        return 1;
    }

    rc += expect_int("invalid variable key", KC_TPL_ERROR, kc_tpl_set_var(ctx, "", "value"));
    rc += expect_int("missing include", KC_TPL_ERROR, kc_tpl_render_string(ctx, "{{@include \"missing.html\"}}", &output));
    free(output);
    output = NULL;
    rc += expect_int("unterminated tag", KC_TPL_ERROR, kc_tpl_render_string(ctx, "{{ title", &output));
    free(output);
    rc += expect_int("close(ctx)", KC_TPL_OK, kc_tpl_close(ctx));
    kc_tpl_options_free(&opts);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "missing helper case\n");
        return 2;
    }

    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "lifecycle") == 0) return case_lifecycle();
    if (strcmp(argv[1], "render-core") == 0) return case_render_core();
    if (strcmp(argv[1], "include-env") == 0 && argc == 3) return case_include_env(argv[2]);
    if (strcmp(argv[1], "failure-paths") == 0) return case_failure_paths();
    fprintf(stderr, "unknown helper case: %s\n", argv[1]);
    return 2;
}
KC_TPL_HELPER
    return 0
}

# Compiles the helper program against the built static library.
# @return 0 on success, 1 on failure.
kc_test_compile_helper() {
    helper_src=$TMP_ROOT/tpl-helper.c
    helper_bin=$TMP_ROOT/tpl-helper
    log=$TMP_ROOT/helper-build.log

    kc_test_write_helper "$helper_src"
    if ! cc -std=c11 -Wall -Wextra -Werror \
        -D_POSIX_C_SOURCE=200809L \
        -include tpl.h \
        -include stdint.h \
        -include stdio.h \
        -include stdlib.h \
        -include string.h \
        -Isrc "$helper_src" "$STATIC_LIB" -o "$helper_bin" > "$log" 2>&1; then
        kc_test_fail "helper compile: expected successful C build, got failure: $(tr '\n' ' ' < "$log")"
        return 1
    fi

    kc_test_pass "helper compile: libtpl test harness linked"
    return 0
}

# Runs one helper case and reports its behavior contract.
# @param $1 Helper case name.
# @param $2 Success message.
# @param $3 Failure message prefix.
# @param $4 Optional helper argument.
# @return 0 on success, 1 on failure.
kc_test_helper_case() {
    case_name=$1
    pass_message=$2
    fail_prefix=$3
    arg=$4
    log=$TMP_ROOT/helper-$case_name.log

    if [ -n "$arg" ]; then
        if "$TMP_ROOT/tpl-helper" "$case_name" "$arg" > "$log" 2>&1; then
            kc_test_pass "$pass_message"
            return 0
        fi
    elif "$TMP_ROOT/tpl-helper" "$case_name" > "$log" 2>&1; then
        kc_test_pass "$pass_message"
        return 0
    fi

    if [ -s "$log" ]; then
        kc_test_fail "$fail_prefix: expected helper success, got failure: $(tr '\n' ' ' < "$log")"
        return 1
    fi

    kc_test_fail "$fail_prefix: expected helper success, got failure with no diagnostic output"
    return 1
}

# Creates include files used by library include resolution tests.
# @return Include root path on stdout.
kc_test_create_include_root() {
    root=$TMP_ROOT/include-root
    mkdir -p "$root"
    printf '%s\n' '<title>{{ page }}</title>' > "$root/page.html"
    printf '%s\n' '<section>{{@include "partial.html"}}</section>' >> "$root/page.html"
    printf '%s\n' '{{@var value "<ok>"}}{{ value }}' > "$root/partial.html"
    printf '%s\n' "$root"
}

# Verifies the CLI renders stdin as a thin integration surface over the library.
# @return 0 on success, 1 on failure.
kc_test_cli_render_integration() {
    out=$(printf '%s' '<p>{{ title }}</p>{{{ raw }}}' | "$BIN" --var title='A&B' --var raw='<hr>')
    expected='<p>A&amp;B</p><hr>'

    if [ "$out" != "$expected" ]; then
        kc_test_fail "CLI render integration: expected '$expected', got '$out'"
        return 1
    fi

    kc_test_pass "CLI render integration: stdin and --var feed libtpl rendering"
    return 0
}

# Runs the full validation suite.
# @return 0 on success, 1 on failure.
kc_test_main() {
    failed=0
    include_root=

    BIN=$(kc_test_binary_path)
    STATIC_LIB=$(kc_test_static_library_path)
    SHARED_LIB=$(kc_test_shared_library_path)
    TMP_ROOT=$(mktemp -d)
    trap kc_test_cleanup EXIT INT TERM

    kc_test_check_artifacts || return 1
    kc_test_compile_helper || return 1
    include_root=$(kc_test_create_include_root)

    kc_test_helper_case "version" "library version: build timestamp is exported" "library version" || failed=$((failed + 1))
    kc_test_helper_case "lifecycle" "library lifecycle: open, close, null guards obey API contract" "library lifecycle" || failed=$((failed + 1))
    kc_test_helper_case "render-core" "library rendering: variables, control flow, blocks, comments render correctly" "library rendering" || failed=$((failed + 1))
    kc_test_helper_case "include-env" "include resolution: KC_TPL_ROOT loads and nested includes render" "include resolution" "$include_root" || failed=$((failed + 1))
    kc_test_helper_case "failure-paths" "failure handling: invalid vars, missing include, and bad syntax fail" "failure handling" || failed=$((failed + 1))
    kc_test_cli_render_integration || failed=$((failed + 1))

    if [ "$failed" -eq 0 ]; then
        return 0
    fi

    return 1
}

kc_test_main
