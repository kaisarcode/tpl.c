# tpl.c — Template Renderer

## Overview
String-based template renderer with includes, scoped variables, named blocks, and control directives. Processes templates with `{{ }}` tag syntax — escaped output, raw output, template comments, include, var assignment, setblock/block, if/else, and foreach directives. Operates on stdin/stdout as CLI or via the `libtpl` C API.

## Architecture
Recursive descent renderer (`kc_tpl_render_internal`) operating on a mutable copy of the template string. Tags are located by scanning for `{{` and `<!--` (HTML comments suppress directive evaluation). Directives prefixed with `{{@` trigger include, var, block, if, or foreach handlers. Variables are stored in a linked scope chain supporting lookups by flat key or dot notation (mapped to underscore-joined flat keys). Block bodies are stored as raw strings in the scope. Output is assembled in a fixed-size buffer within the context.

## Directory Layout
| Path | Contents |
|------|----------|
| `src/tpl.c` | CLI entry point (stdin/stdout) |
| `src/libtpl.c` | Template renderer implementation |
| `src/tpl.h` | Public API header |
| `Makefile` | Cross-compilation build |
| `CMakeLists.txt` | CMake build definition |
| `test.sh` | Functional test suite |
| `README.md` | Usage and API reference |

## Data Model
### Internal Structures
| Symbol | Type | Role |
|--------|------|------|
| `kc_tpl_t` | `struct kc_tpl` (opaque) | Renderer context |
| `kc_tpl.root` | `char[4096]` | Include base directory |
| `kc_tpl.out` | `char[262144]` | Output buffer |
| `kc_tpl.out_n` | `size_t` | Current output length |
| `kc_tpl.error` | `char[256]` | Error message buffer |
| `kc_tpl.scope` | `kc_tpl_scope_t` | Root scope |
| `kc_tpl_var_t` | `struct { char key[64]; char *val; }` | Scope variable |
| `kc_tpl_block_t` | `struct { char name[64]; char body[8192]; }` | Named block |
| `kc_tpl_scope_t` | `struct { vars[128]; var_n; blocks[64]; block_n; parent*; }` | Scope with parent chain |
| `kc_tpl_comment_open` | `static const char*` = `{{/*` | Template comment opener |
| `kc_tpl_comment_close` | `static const char*` = `*/}}` | Template comment closer |

### Hard Limits
| Limit | Value | Symbol |
|-------|-------|--------|
| Root path buffer | 4096 | `KC_TPL_ROOT_CAP` |
| Output buffer | 262144 | `KC_TPL_OUTPUT_CAP` |
| Eval value buffer | 2048 | `KC_TPL_EVAL_CAP` |
| Variable key length | 63 | `kc_tpl_var_t.key[64]` |
| Block name length | 63 | `kc_tpl_block_t.name[64]` |
| Block body size | 8192 | `kc_tpl_block_t.body[8192]` |
| Vars per scope | 256 | `scope->vars[256]` |
| Blocks per scope | 64 | `scope->blocks[64]` |
| Include full path | 8192 | (hardcoded in `kc_tpl_load`) |
| Include file content | 8191 | (hardcoded in `kc_tpl_load`) |
| Dot path buffer | 512 | (hardcoded in `kc_tpl_eval_dot`) |
| List items | 64 | `out[64][128]` in `kc_tpl_list` |
| Tag buffer | 512 | (hardcoded in `kc_tpl_render_internal`) |

## Input/Processing Format
**Template Syntax**:

| Directive | Syntax | Description |
|-----------|--------|-------------|
| Escaped output | `{{ expr }}` | HTML-escaped variable/interpolation |
| Raw output | `{{{ expr }}}` | Unescaped output |
| Template comment | `{{/* comment */}}` | Stripped from output |
| Variable assignment | `{{@var name expr}}` | Sets variable in current scope |
| Include | `{{@include "path"}}` | Includes file relative to root dir |
| Block definition | `{{@setblock name}}...{{@endsetblock}}` | Stores body as named block |
| Block render | `{{@block name}}` | Renders named block |
| Block with props | `{{@block name { key: val }}}` | Renders block with inline scope variables |
| Conditional | `{{@if expr}}...{{@else}}...{{@endif}}` | Truthy evaluation |
| Foreach | `{{@foreach item in list}}...{{@endforeach}}` | Iterates over CSV/array |

**Variables**: String-based. Dot notation (`user.name`) maps to underscore-joined flat key (`user_name`). Also resolves through `foreach` aliases: given `item` aliased to `item_1`, `item.title` resolves `item_1_title`. Lookup traverses scope chain to parent.

**Lists**: Comma-separated values or bracket syntax `[a,b,c]`. Quoted strings supported: `["a b", c]`.

**Truthiness**: Non-empty string not equal to `"0"`, `"false"`, or `"null"`.

**HTML comments**: Directives inside `<!-- -->` are not evaluated — passed through verbatim.

## Public API
| Function | Signature | Description |
|----------|-----------|-------------|
| `kc_tpl_open` | `kc_tpl_t *kc_tpl_open(void)` | Allocate and initialize context (root = `"."`, error = `"ok"`) |
| `kc_tpl_close` | `void kc_tpl_close(kc_tpl_t *ctx)` | Release context and free all variable values |
| `kc_tpl_set_root` | `int kc_tpl_set_root(kc_tpl_t *ctx, const char *root)` | Set include base directory |
| `kc_tpl_set_var` | `int kc_tpl_set_var(kc_tpl_t *ctx, const char *key, const char *value)` | Set variable in root scope |
| `kc_tpl_render_string` | `int kc_tpl_render_string(kc_tpl_t *ctx, const char *input, char **output)` | Render template to owned output string |
| `kc_tpl_strerror` | `const char *kc_tpl_strerror(const kc_tpl_t *ctx)` | Return error description |

## CLI
| Flag | Description |
|------|-------------|
| `--root <dir>` | Base directory for include resolution (default: cwd) |
| `--var <key=value>` | Inject template variable (repeatable) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

**Exit codes**: 0 = success, 1 = error.

## Build
| Target | Description |
|--------|-------------|
| `make` (default: `native`) | Build for host architecture |
| `make all` | Build all cross-compilation targets |
| `make x86_64/linux` | Linux x86_64 |
| `make aarch64/android` | Android ARM64 (NDK) |
| `make test` | Run `test.sh` |
| `make clean` | Remove `.build/` |

Artifacts: `bin/{arch}/{platform}/tpl`, `libtpl.a`, `libtpl.so` (`.dll` on Windows).

## Error Handling
| Code | Value | Meaning |
|------|-------|---------|
| `KC_TPL_OK` | 0 | Success |
| `KC_TPL_ERROR` | -1 | Generic failure |

Errors returned via `kc_tpl_strerror(ctx)` with messages including: `invalid argument`, `output too large`, `out of memory`, `invalid root`, `expression evaluation failed`, `variable assignment failed`, `invalid block directive`, `missing endif`, `missing endforeach`, `invalid foreach`, `include not found`, `include read failed`, `unterminated tag`, `unterminated raw tag`, `unterminated comment`.

CLI prints errors to stderr with `tpl:` prefix.

## Constraints
- Fixed-size output buffer (256 KB) — templates producing larger output fail with `output too large`.
- Fixed-size block bodies (8192 bytes) — blocks exceeding this are truncated.
- No automatic escaping in `{{{ }}}` — caller must sanitize.
- Variables are string-only — no numeric operations, no type coercion.
- Dot notation is a flat-key alias layer — no true nested object traversal.
- Includes are not scoped — included templates share the caller's variable scope.
- No expression operators (comparison, arithmetic, ternary, filters).
- No template inheritance (extends) — use `setblock` + `include` pattern.
