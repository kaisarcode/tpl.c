# tpl.c Design

## Purpose

`tpl.c` renders small string-based templates through a native C library and a
stdin/stdout CLI.

It provides interpolation, local includes, scoped variables, named blocks,
conditionals, and bounded iteration without embedding a general-purpose
language or application framework.

## Architecture

The rendering path is direct:

1. A context stores root configuration, variables, blocks, output, and errors.
2. `kc_tpl_render_string()` duplicates the mutable template source.
3. `kc_tpl_render_internal()` scans literal text and `{{` tags.
4. Directive-specific helpers evaluate values or recursively render bounded
    bodies and includes.
5. Output accumulates in the context's fixed buffer.
6. A caller-owned copy is returned.

The CLI owns argument parsing and byte-framed stdin/stdout processing. The
library owns all template semantics.

## Lifecycle and Ownership

`kc_tpl_options_default()` returns caller-owned options with no root allocation
and delimiter byte 4.

`kc_tpl_options_load_env()` may allocate the root string. The caller releases
it with `kc_tpl_options_free()`.

`kc_tpl_open()` allocates an opaque context and copies the root option. The
context defaults its include root to `.` when no root is supplied.

`kc_tpl_set_var()` copies values into the context. `kc_tpl_render_string()`
returns a newly allocated string owned by the caller. `kc_tpl_close()` releases
all context variables, options, and context storage.

## Variable Model

Variables are strings identified by names shorter than 64 bytes. Setting an
existing name replaces its owned value.

Lookup walks the current scope and then parent scopes. Missing values resolve
to the empty string.

Dot notation does not traverse objects. `object.field` maps to the flat key
`object_field`. In a foreach scope, an alias value may provide the prefix, so
`item.title` can resolve `item_1_title` when `item` contains `item_1`.

Quoted string expressions remove their surrounding double quotes. The literal
strings `true`, `false`, and `null` evaluate to themselves.

## Escaped and Raw Output

`{{ expr }}` escapes ampersand, less-than, greater-than, and double-quote bytes.

`{{{ expr }}}` writes the evaluated string without escaping. This is an
explicit trust boundary controlled by the template author and caller.

The renderer does not sanitize HTML or establish browser content policy.

## Directives

The directive set is intentionally small:

- `@include` evaluates a path and renders the loaded file in the current scope;
- `@var` assigns an evaluated string in the current scope;
- `@setblock` stores a named template body;
- `@block` renders a named body in a child scope;
- `@if`, optional `@else`, and `@endif` select one body;
- `@foreach` and `@endforeach` render a body for each string item.

Matching close directives are found with explicit nesting depth. Template
comments are skipped while matching. Missing close directives produce
context-owned errors.

Unknown directive tags have no defined extension dispatch. There is no plugin
or user-function mechanism.

## Scopes

The context contains the root scope. Block properties and each foreach
iteration create a temporary child with a pointer to its parent.

Child values shadow parent values. Child allocations are released immediately
after rendering that block or iteration.

Named blocks are resolved through the same scope chain. Each scope stores at
most 64 blocks with fixed name and body storage.

## Blocks and Properties

`@setblock` captures source text rather than pre-rendered output. Rendering a
block evaluates its body against a child scope at use time.

Inline properties accept brace or bracket forms containing comma-separated
`key: expression` pairs. Each expression is evaluated in the parent scope and
stored in the child.

Blocks are local template composition, not inheritance classes or reusable
runtime plugins.

## Lists and Foreach

Lists are strings containing comma-separated items, optionally wrapped in
brackets. Quoted item edges are removed. At most 64 items of at most 127 bytes
are materialized.

Each iteration stores the current item string under the declared alias in a
new child scope, renders the captured body, and releases the child.

There are no collection objects, indexes, maps, sorting, filtering, or
parallel iteration.

## Includes

Includes evaluate one path expression, concatenate it with the configured root,
read up to 8191 bytes from that local file, and render it in the current scope.

The root is a path resolution base. The implementation does not canonicalize
paths or guarantee confinement against traversal. It must not be described as
a sandbox.

Includes are synchronous and local. There is no network fetching, registry,
cache, watcher, or package resolver.

## Comments

`{{/* comment */}}` is removed from rendered output.

Template-looking text inside an HTML `<!-- -->` comment is not evaluated. The
HTML comment itself remains in output.

## Limits and Errors

The context uses explicit fixed limits:

- 262144-byte output storage;
- 2048-byte evaluated values;
- 4096-byte root storage;
- 8191-byte include content;
- 64 blocks per scope;
- 8192-byte block and captured control bodies;
- 64 foreach items of 128 bytes each;
- names shorter than 64 bytes.

Output overflow fails with `output too large`. Missing includes, malformed
directives, assignment failures, and unterminated structures update the
context error buffer returned by `kc_tpl_strerror()`.

The renderer may have accumulated partial internal output when it fails, but no
caller-owned output is returned unless the complete render succeeds.

## Resident CLI

The CLI loads options from defaults and environment, applies CLI overrides,
then keeps one context across requests.

Requests arrive through stdin and end at the configured byte. Each successful
render is written to stdout followed by the same delimiter. Empty input ends
the loop.

Context variables and root configuration persist across requests. Template
directives may update root-scope variables and blocks for subsequent renders.

This is a foreground filter, not a daemon or remote renderer. A generic control
socket was deliberately removed.

## Configuration

Configuration precedence is:

1. Built-in defaults.
2. `KC_TPL_ROOT` and `KC_TPL_UNTIL` environment values.
3. `--root`, repeated `--var`, and `--until` CLI flags.

The CLI accepts delimiter values from 0 through 255 and rejects malformed
values and unknown options.

## Signals and Concurrency

Contexts own stop flags. The library and CLI do not install, receive, or
dispatch operating-system signals.

The public API does not promise thread-safe concurrent access to one context.

## Portability and Composition

The implementation is portable C11 with small platform branches for descriptor
I/O and signals. The build emits CLI, static library, and shared library
artifacts.

The CLI composes through stdin, stdout, stderr, byte framing, and exit status.
The library composes through direct C calls and caller-owned strings.

HTTP serving, file watching, content storage, data decoding, and application
policy belong in separate tools.

## Non-Goals

`tpl.c` is not intended to provide:

- a general-purpose expression or scripting language;
- arbitrary code or shell execution;
- plugins, filters, or dynamic modules;
- JSON, YAML, database, or reflection object models;
- template inheritance frameworks;
- network includes or package resolution;
- sandboxing of untrusted templates;
- HTML sanitization;
- web serving, sessions, routing, or middleware;
- CMS, collaboration, accounts, or permissions;
- hot reload or file watching;
- remote rendering or hosted infrastructure;
- telemetry or analytics;
- a resident control plane.

These exclusions define the renderer's small role. They are not an unfinished
roadmap.

## Change Criteria

A proposed change should answer:

1. Which concrete template requires the behavior?
2. Can the responsibility remain in another composable tool?
3. Does syntax or escaping compatibility change?
4. Does scope ownership remain explicit?
5. Does include behavior expose new filesystem or network authority?
6. Which fixed limits change and why?
7. How do malformed and nested forms fail?
8. Does rendering remain deterministic and locally inspectable?
9. Does resident framing remain byte-exact?
10. Does the feature turn the renderer into a language or framework?

Changes justified mainly by framework parity, enterprise readiness, or
hypothetical future use should be rejected.

## Core Invariants

The project is defined by:

- string-only values and simple scoped lookup;
- escaped output by default and explicit raw output;
- local synchronous includes;
- bounded blocks, lists, includes, values, and output;
- caller-owned rendered strings;
- context-owned variables and errors;
- direct template syntax without a general runtime;
- foreground stdin/stdout composition;
- no mandatory network or service infrastructure;
- portable, inspectable C11 code.

The renderer should remain smaller and more explicit than the applications that
compose it.
