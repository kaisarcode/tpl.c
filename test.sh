#!/bin/sh
# test.sh
# Summary: Validation suite for tpl functionality.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: https://www.gnu.org/licenses/gpl-3.0.html

# Prints one failure line.
# @param $1 Failure message.
# @return 1 on failure.
kc_test_fail() {
    printf '\033[31m[FAIL]\033[0m %s\n' "$1"
    return 1
}

# Prints one success line.
# @param $1 Success message.
# @return 0 on success.
kc_test_pass() {
    printf '\033[32m[PASS]\033[0m %s\n' "$1"
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

# Verifies the binary exists and is executable.
# @return 0 on success, 1 on failure.
kc_test_check_binary() {
    if [ ! -x "$BIN" ]; then
        kc_test_fail "binary not found: $BIN"
        return 1
    fi

    return 0
}

# Verifies the library artifacts exist.
# @return 0 on success, 1 on failure.
kc_test_check_libraries() {
    if [ ! -f "$STATIC_LIB" ]; then
        kc_test_fail "static library not found: $STATIC_LIB"
        return 1
    fi

    if [ ! -f "$SHARED_LIB" ]; then
        kc_test_fail "shared library not found: $SHARED_LIB"
        return 1
    fi

    kc_test_pass "libraries"
}

# Tests help and version flags.
# @return 0 on success, 1 on failure.
kc_test_cli() {
    if ! "$BIN" --help > /dev/null 2>&1; then
        kc_test_fail "cli: --help failed"
        return 1
    fi

    if ! "$BIN" -v > /dev/null 2>&1; then
        kc_test_fail "cli: -v failed"
        return 1
    fi

    if "$BIN" --unknown > /dev/null 2>&1; then
        kc_test_fail "cli: unknown flag should fail"
        return 1
    fi

    if "$BIN" --root > /dev/null 2>&1; then
        kc_test_fail "cli: missing --root value should fail"
        return 1
    fi

    if "$BIN" --var > /dev/null 2>&1; then
        kc_test_fail "cli: missing --var value should fail"
        return 1
    fi

    if "$BIN" --var broken > /dev/null 2>&1; then
        kc_test_fail "cli: invalid --var format should fail"
        return 1
    fi

    kc_test_pass "cli"
}

# Tests escaped variable output.
# @return 0 on success, 1 on failure.
kc_test_escape() {
    out=$(printf '%s' '<h1>{{ title }}</h1>' | "$BIN" --var title='A&B')
    if [ "$out" != "<h1>A&amp;B</h1>" ]; then
        kc_test_fail "escape: output mismatch"
        return 1
    fi

    kc_test_pass "escape"
}

# Tests raw (unescaped) output tag.
# @return 0 on success, 1 on failure.
kc_test_raw() {
    out=$(printf '%s' '{{{ raw }}}' | "$BIN" --var raw='<b>x</b>')
    if [ "$out" != "<b>x</b>" ]; then
        kc_test_fail "raw: output mismatch"
        return 1
    fi

    kc_test_pass "raw"
}

# Tests if/else directive.
# @return 0 on success, 1 on failure.
kc_test_if() {
    out=$(printf '%s' '{{@if show}}yes{{@else}}no{{@endif}}' | "$BIN" --var show=true)
    if [ "$out" != "yes" ]; then
        kc_test_fail "if: branch mismatch"
        return 1
    fi

    out=$(printf '%s' '{{@if show}}yes{{@else}}no{{@endif}}' | "$BIN")
    if [ "$out" != "no" ]; then
        kc_test_fail "if: else branch mismatch"
        return 1
    fi

    kc_test_pass "if"
}

# Tests foreach directive.
# @return 0 on success, 1 on failure.
kc_test_foreach() {
    out=$(printf '%s' '{{@foreach item in items}}<i>{{ item }}</i>{{@endforeach}}' | "$BIN" --var items='[alpha,beta]')
    if [ "$out" != "<i>alpha</i><i>beta</i>" ]; then
        kc_test_fail "foreach: output mismatch"
        return 1
    fi

    kc_test_pass "foreach"
}

# Tests dot-notation variable lookup.
# @return 0 on success, 1 on failure.
kc_test_dot() {
    out=$(printf '%s' '{{ user.name }}' | "$BIN" --var user_name=Kaisar)
    if [ "$out" != "Kaisar" ]; then
        kc_test_fail "dot: lookup mismatch"
        return 1
    fi

    out=$(printf '%s' '{{@foreach item in items}}<b>{{ item.title }}</b>{{@endforeach}}' | \
        "$BIN" --var items='[item_1,item_2]' --var item_1_title=One --var item_2_title=Two)
    if [ "$out" != "<b>One</b><b>Two</b>" ]; then
        kc_test_fail "dot: foreach alias lookup mismatch"
        return 1
    fi

    kc_test_pass "dot"
}

# Tests setblock and block directives.
# @return 0 on success, 1 on failure.
kc_test_block() {
    out=$(printf '%s' '{{@setblock card}}<b>{{ name }}</b>{{@endsetblock}}{{@block card}}' | "$BIN" --var name=Kaisar)
    if [ "$out" != "<b>Kaisar</b>" ]; then
        kc_test_fail "block: render mismatch"
        return 1
    fi

    out=$(printf '%s' '{{@setblock nav_item}}<a href="{{href}}">{{label}}</a>{{@endsetblock}}{{@block nav_item [ "href": "/about", "label": "About" ]}}' | "$BIN")
    if [ "$out" != '<a href="/about">About</a>' ]; then
        kc_test_fail "block: props render mismatch"
        return 1
    fi

    kc_test_pass "block"
}

# Tests include directive with --root.
# @return 0 on success, 1 on failure.
kc_test_include() {
    TEST_DIR=$(mktemp -d)
    mkdir -p "$TEST_DIR/partial"

    printf '%s\n' '<title>{{ page }}</title><section>{{@include "partial/item.html"}}</section>' \
        > "$TEST_DIR/page.html"
    printf '%s\n' '{{@var value "<ok>"}}<span>{{ value }}</span>' \
        > "$TEST_DIR/partial/item.html"

    out=$(printf '%s' '{{@include "page.html"}}' | "$BIN" --root "$TEST_DIR" --var page=Home)
    expected=$(printf '<title>Home</title><section><span>&lt;ok&gt;</span>\n</section>')
    if [ "$out" != "$expected" ]; then
        kc_test_fail "include: output mismatch"
        rm -rf "$TEST_DIR"
        return 1
    fi

    out=$(printf '%s' '{{@include tpl_file}}' | "$BIN" --root "$TEST_DIR" --var page=Home --var tpl_file='page.html')
    if [ "$out" != "$expected" ]; then
        kc_test_fail "include: variable path mismatch"
        rm -rf "$TEST_DIR"
        return 1
    fi

    rm -rf "$TEST_DIR"
    kc_test_pass "include"
}

# Tests that HTML comments suppress directive rendering.
# @return 0 on success, 1 on failure.
kc_test_html_comment() {
    out=$(printf '%s' '<div><!-- {{@if show}}x{{@endif}} --></div>' | "$BIN")
    if [ "$out" != "<div><!-- {{@if show}}x{{@endif}} --></div>" ]; then
        kc_test_fail "html_comment: directive inside comment should not render"
        return 1
    fi

    kc_test_pass "html_comment"
}

# Tests that template comments are stripped from output.
# @return 0 on success, 1 on failure.
kc_test_tpl_comment() {
    out=$(printf '%s' 'A{{/* hidden */}}B' | "$BIN")
    if [ "$out" != "AB" ]; then
        kc_test_fail "tpl_comment: comment not stripped"
        return 1
    fi

    kc_test_pass "tpl_comment"
}

# Tests @var directive.
# @return 0 on success, 1 on failure.
kc_test_var_directive() {
    out=$(printf '%s' '{{@var greeting "Hello"}}{{ greeting }}' | "$BIN")
    if [ "$out" != "Hello" ]; then
        kc_test_fail "var_directive: output mismatch"
        return 1
    fi

    kc_test_pass "var_directive"
}

# Runs the full validation suite.
# @return 0 on success, 1 on failure.
kc_test_main() {
    failed=0

    BIN=$(kc_test_binary_path)
    STATIC_LIB=$(kc_test_static_library_path)
    SHARED_LIB=$(kc_test_shared_library_path)

    kc_test_check_binary || exit 1
    kc_test_check_libraries || failed=$((failed + 1))

    kc_test_cli           || failed=$((failed + 1))
    kc_test_escape        || failed=$((failed + 1))
    kc_test_raw           || failed=$((failed + 1))
    kc_test_if            || failed=$((failed + 1))
    kc_test_foreach       || failed=$((failed + 1))
    kc_test_dot           || failed=$((failed + 1))
    kc_test_block         || failed=$((failed + 1))
    kc_test_include       || failed=$((failed + 1))
    kc_test_html_comment  || failed=$((failed + 1))
    kc_test_tpl_comment   || failed=$((failed + 1))
    kc_test_var_directive || failed=$((failed + 1))

    return $failed
}

kc_test_main
