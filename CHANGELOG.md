# Changelog

## Unreleased

- `generate_cli_reference.py --neutral-availability [IDS]` describes every
  command, or the identifiers given, as available whatever the host: a
  command `.unavailable` on some hosts (maelys-oci's Linux-only
  `unpack-rootfs`) made the committed contract host-dependent, and the
  product had to normalize it with a script of its own.

## 0.5.6 - 2026-09-03

Feedback from the Maelys OCI open-core split (additive, hence a patch
release: the CMake package is compatible within a minor and consumers
request `0.5`):

- `maelys_cli_catalog_concat()`, `maelys_cli_catalog_part_t` and
  `MAELYS_CLI_CATALOG_PART`: a catalog composed from parts at startup.
  A later part may provide a command that an earlier part declares
  `.unavailable`, replacing it in place so the extended build offers it at
  the same position in help and describe. Any other repeated identifier is
  refused with `EEXIST`: composition never shadows a real command silently.
  The `maelys` dispatcher composes its own catalog with it.
- Regenerate the release workflow with maelys-release 0.2.8 (the tap publish
  job no longer trips on a duplicate formula class).

## 0.5.5 - 2026-09-03

- The `maelys` formula description no longer starts with the formula name
  (`brew style` refused it, so 0.5.4 published no formula) and the release
  workflow is regenerated with maelys-release 0.2.6, which taps
  `maelys-dev/tap` before building bottles so `libmaelys-cli` finds
  `libmaelys-json`.

## 0.5.4 - 2026-09-03

- Regenerate the release workflow with maelys-release 0.2.5. The `v0.5.3`
  tag exists but produced no release: the publish job of the socle expected
  deb and rpm packages that this repository does not ship.

## 0.5.3 - 2026-09-03

- Release through the shared maelys-release workflows and publish two
  Homebrew formulas: `maelys`, the command alone (`make install-dispatcher`),
  and `libmaelys-cli`, the framework to build a product CLI (`make
  install-sdk`); `make install` still installs both. Both formulas build
  against the `libmaelys-json` formula. `scripts/package-release.sh TARGET`
  stages the installed tree; `adapter/MAELYS_JSON_PIN` and
  `scripts/checkout-json.sh` record and fetch the pinned maelys-json.

## 0.5.2 - 2026-09-03

Feedback from the Maelys Git 0.5 integration:

- `maelys-cli-reference` takes `--title`, `--intro`/`--intro-file`,
  `--columns` and `--global-label`, so a product keeps the language and
  header of its generated reference.
- CMake looks for maelys-json only when `MAELYS_CLI_BUILD_EXTENSION` is on;
  a core-only consumer (`-DMAELYS_CLI_BUILD_EXTENSION=OFF`) never configures
  the sibling project.
- `MAELYS_CLI_DEFAULT_OF(constant)` declares `default_text` from a numeric
  constant of the product library, giving a default a single source.
- `docs/topics.tsv` and `scripts/doc-topics-check.sh` (part of
  `make check`): a topic coverage contract listing, per document, the
  keywords it must mention, so documentation cannot lag behind a feature.

## 0.5.1 - 2026-09-02

- Documentation only: the installed agent texts, the product templates,
  `docs/agent-cli.md`, `docs/abi.md` and `SECURITY.md` describe the 0.5.0
  dependency boundary (dependency-free core, `libmaelys_cli_extension.a`
  on maelys-json, one archive copy per executable), the writer's UTF-8
  strictness and the completion changes.

## 0.5.0 - 2026-09-02

Self-review of the framework and dependency boundary:

- Manifest discovery moves to `libmaelys_cli_extension.a`, which reads
  manifests through maelys-json (bounded parsing, duplicate keys and
  invalid UTF-8 refused). The core `libmaelys_cli.a` stays dependency-free
  and no longer exposes a document reader: `maelys_cli_json_object_get`,
  `maelys_cli_json_decode_string` and `maelys_cli_json_decode_unsigned` are
  removed. pkg-config `maelys-cli-extension` and CMake
  `maelys::cli_extension` declare the dependency; archives never embed it.
- The core writer refuses invalid UTF-8 so envelopes are always valid JSON.
- `maelys_cli_process_run` closes inherited descriptors with
  `close_range` on Linux and `closefrom` on the BSDs.
- Completion: `help` and `describe` complete command identifiers,
  unavailable commands are never offered, and the `maelys` dispatcher
  forwards completion of an external command to that command's own
  `__complete`.

## 0.4.2 - 2026-09-02

- The installed agent guide lists every handler accessor;
  `scripts/agent-doc-check.sh` (part of `make check`) keeps the guide in
  step with the macros of `catalog.h` and the accessors of `app.h`.
- Framework change procedure for agents working on this repository:
  `AGENTS.md` and the `maelys-cli-framework` Claude skill.

## 0.4.1 - 2026-09-02

- Documentation only: the installed agent instructions (`AGENTS.md` /
  `CLAUDE.md` block, guide, Claude skill), the product templates and
  `docs/agent-cli.md` now cover every feature added in 0.2 to 0.4 (typed
  kinds and operands, dependency groups, typed defaults, unavailable
  commands, `maelys_cli_replied`, helper resolution, trusted emitters,
  completion, `MAELYS_CLI_FORMAT`, minimal `describe`). Run
  `maelys agents install DIR --apply` again in consumer projects.

## 0.4.0 - 2026-09-02

Feedback from the Maelys Git migration:

- Synopses are derived dynamically (`maelys_cli_command_synopsis_alloc`);
  the catalog validation measures every synopsis against
  `MAELYS_CLI_MAX_SYNOPSIS` (4096) and names the offending command. Required
  options now precede optional ones in the derived synopsis.
- Dependency groups: `.depends_on_all` (NULL-terminated list) and `.group`
  (all-or-none), enforced by the parser and exposed in `describe`
  (`requires` lists, `all-or-none` constraints, per-option `group`).
- Typed defaults: `default_text` is validated against the option's kind at
  startup and returned by `maelys_cli_option_unsigned/integer/choice` and
  `maelys_cli_option_or` when the option is absent; handlers no longer
  repeat defaults.
- Unavailable commands: `.unavailable = "reason"` replaces a failing
  handler; the command stays in `describe` (`available: false`,
  `unavailableReason`) and fails with `UNSUPPORTED`.
- `maelys_cli_succeed_trusted` and `maelys_cli_emit_record_trusted` write
  serializer-guaranteed JSON verbatim in compact and jsonl modes.
- CMake package: `add_subdirectory`, FetchContent or
  `find_package(maelys-cli)` provide `maelys::cli`, `MAELYS_CLI_EMBED` and
  `MAELYS_CLI_REFERENCE`; `make cmake-check` proves it.
- `maelys-cli-reference` (the reference generator) is installed with
  `maelys-cli-embed`.

## 0.3.0 - 2026-09-02

Feedback from the Egress migration:

- Shell completion generated from the catalog: `PROGRAM completion
  bash|zsh|fish` prints a shim that calls the hidden `__complete` command,
  which returns candidates for command words, options, `--option=value`
  choices, digest algorithm prefixes and typed operands.
- Typed operands: `maelys_cli_operand_t` carries a kind, choices and
  limits (`MAELYS_CLI_OPERAND_CHOICE`, `MAELYS_CLI_OPERAND_KIND`), validated
  by the parser, exposed in `describe` and readable through
  `maelys_cli_operand_choice()`, `maelys_cli_operand_unsigned()` and
  `maelys_cli_operand_integer()`.
- `describe COMMAND_ID` is minimal: `globalOptions`, `output` and
  `invariants` are only part of the inventory forms.
- `MAELYS_CLI_FORMAT=json|text` selects the default rendering; for
  protocol-stream commands it only shapes the failure envelope on stderr.
- `maelys_cli_json_string()` refuses `NULL` instead of writing `null`.
- The reference generator omits product and framework versions unless
  `--include-versions`, so `contract-check` is now part of `make check`.
- The public headers state that the `maelys_cli_` / `MAELYS_CLI_` namespace
  is reserved for the framework.

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
