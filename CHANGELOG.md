# CHANGELOG

## v1.1.1

- Fixed `tpl_signal_listener` to restore default signal behavior (SIG_DFL)
  when no `on_signal` handler is registered. Moved `<signal.h>` include to
  be unconditional for MinGW compatibility.

## v1.1.0

- Added data-driven configuration lifecycle through `kc_tpl_options_t`.
- Added `kc_tpl_options_default()`, `kc_tpl_options_load_env()`, and `kc_tpl_options_free()` to the public API.
- Refactored `kc_tpl_open()` to take `kc_tpl_options_t`.
- CLI is now decoupled from `libtpl`; configuration is initialized through options, then overridden by flags.
- Added signal listener lifecycle: `kc_tpl_on_signal()`, `kc_tpl_raise_signal()`, `kc_tpl_listen_signals()`, `kc_tpl_listen_signal()`, and `kc_tpl_signal_listener()`.

## v1.0.0

- Published the stable baseline release.
- Provided template rendering with includes, scoped variables, named blocks, and control directives.
- Supported escaped output, raw output, template comments, variable assignment, include, setblock/block, if/else, and foreach directives.
