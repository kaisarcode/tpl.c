# tpl.c - Template Renderer

`tpl.c` is a simple template renderer with includes, scoped variables, blocks, and basic control directives. It provides `libtpl` for C callers and a `tpl` CLI that reads the template from standard input and writes the rendered output to standard output.

---

## CLI

### Examples

Render a template with variables:

```bash
echo '<h1>{{ title }}</h1>' | ./bin/x86_64/linux/tpl --var title=Home
```

Render with raw (unescaped) output:

```bash
echo '{{{ body }}}' | ./bin/x86_64/linux/tpl --var body='<b>bold</b>'
```

Render a file with includes:

```bash
./bin/x86_64/linux/tpl --root ./views --var page=Home < views/page.html
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `--root <dir>` | Base directory for `{{@include ...}}` path resolution (default: cwd) |
| `--var <key=value>` | Inject a template variable (repeatable) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

## Template Syntax

| Directive | Description |
| :--- | :--- |
| `{{ expr }}` | HTML-escaped output |
| `{{{ expr }}}` | Raw (unescaped) output |
| `{{ object.field }}` | Dot notation over flat `object_field` keys and foreach aliases |
| `{{/* comment */}}` | Template comment, stripped from output |
| `{{@include "path"}}` | Include a file relative to `--root` |
| `{{@var name expr}}` | Set a variable in the current scope |
| `{{@setblock name}} ... {{@endsetblock}}` | Define a named block |
| `{{@block name}}` | Render a named block |
| `{{@block name { key: val }}}` | Render a block with inline props |
| `{{@if expr}} ... {{@else}} ... {{@endif}}` | Conditional rendering |
| `{{@foreach item in list}} ... {{@endforeach}}` | Iterate over a comma-separated list or `[a,b,c]` |

Variables are string-based. Lists are passed as CSV or `[a,b,c]`. Truthy values are non-empty strings except `0`, `false`, and `null`. Directives inside HTML comments (`<!-- -->`) are not evaluated.

---

## Public API

```c
#include "tpl.h"

kc_tpl_t *ctx = kc_tpl_open();
char *output = NULL;

kc_tpl_set_root(ctx, ".");
kc_tpl_set_var(ctx, "title", "Home");
kc_tpl_render_string(ctx, "<h1>{{ title }}</h1>", &output);

free(output);
kc_tpl_close(ctx);
```

---

## Lifecycle

- `kc_tpl_open()` - allocates and returns a new renderer context owned by the caller.
- `kc_tpl_set_root()` - configures include path resolution for the context.
- `kc_tpl_set_var()` - stores string variables in the context scope.
- `kc_tpl_render_string()` - renders one template into a caller-owned output buffer.
- `kc_tpl_close()` - releases the context and all associated variable storage.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

## Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture, while the targets below build the full matrix or a specific target.

```bash
make all
make x86_64/linux
make x86_64/windows
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

**Author:** KaisarCode

**Email:** <kaisarcode@gmail.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
