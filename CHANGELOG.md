# Changelog

## 0.2.0 - 2026-09-02

Feedback from the first migrations (Maelys Git, Warden, Egress):

- New value kinds `absolute-path` (`MAELYS_CLI_ABSOLUTE_PATH`) and `digest`
  (`MAELYS_CLI_DIGEST`, `ALGORITHM:HEX` with the length implied by the
  algorithm), so handlers no longer re-validate strings.
- `maelys_cli_replied()` exposes the reply state for helpers that may reply;
  the guide documents the pattern.
- `maelys_cli_resolve_helper()` and `context->executable` expose the
  delegate search order and `argv[0]` to handlers.
- Error messages grow to 4096 bytes and hints to 1024
  (`MAELYS_CLI_MAX_ERROR_MESSAGE`, `MAELYS_CLI_MAX_ERROR_HINT`).
- Documented that `--dry-run`/`--plan` are refused only on commands that
  declare `--apply`.

## 0.1.0 - 2026-09-02

- Licensing: framework code under MPL-2.0; agent texts and product
  templates under CC0-1.0 so consumers can copy and edit them freely
  (`LICENSING.md`).
- Initial release of `libmaelys_cli`: product-neutral value parsing,
  environment overlays, bounded file I/O with atomic writes, SHA-256,
  dependency-free JSON writer/validator/formatter, terminal detection and
  safe process invocation extracted from the Warden CLI.
- Declarative command catalog with typed operands and options, causal
  validation, derived synopsis, `help`, `version` and machine-readable
  `describe` following the `agent-cli/v2` envelope of Maelys Git.
- Rendering modes `text`, `json` and `jsonl`, stable error codes and exit
  codes `0`, `1`, `2`, plan/apply transactions with `--apply`.
- External commands: delegate commands resolved beside the executable or in
  declared helper directories, executed with `execve` and no shell.
- `maelys` dispatcher discovering external commands through verified
  `maelys.cli-extension/v1` manifests, and `maelys agents install|status`
  to manage Claude Code and Codex instructions in consumer projects.
- Declaration macros (`MAELYS_CLI_READ`, `MAELYS_CLI_TRANSACTION`,
  `MAELYS_CLI_STRING`, `MAELYS_CLI_SIZE`, ...) over designated initializers,
  and `maelys-cli-embed` to embed JSON Schema files and other texts as C
  symbols, so catalogs never carry hand-escaped JSON.
- `MAELYS_CLI_PROTOCOL_STREAM` names the protocol owning a stream command's
  stdio (`protocol` in `describe`); `MAELYS_CLI_HEX_OR` accepts two
  hexadecimal lengths for object identifiers; the reference generator takes
  the binaries to aggregate as arguments.
- CI workflow for Linux amd64/arm64 (clang and GCC) and macOS; Egress
  migration path and the JSON writer decision documented. Verified with
  GCC 14 on Linux (glibc feature macros, `/proc/self/exe`) and clang on
  macOS; the option field is named `depends_on` because `requires` is a
  C++20 keyword (the `describe` member stays `requires`).
- `make contract-check` rejects a stale generated reference (locally and in
  CI); short product templates for `command-conventions.md` and
  `agent-cli.md` installed under `share/maelys-cli/templates/`.
- Reference product CLI `maelys-hello`, unit tests, end-to-end CLI tests,
  C++ header gate, sanitizer target and reference generator.
