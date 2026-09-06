# Changelog

## 0.5.19 - 2026-09-06

- agent-cli-spec pinned at v2.3.1: the `--prefix` grammar is written with a
  plain group, as the common dialect requires; the parser still accepts a
  product pattern spelled with `(?:`.
- maelys-release v0.15.1 adopted: release assets through a protected draft,
  `workflow_dispatch` replays the complete flow for an existing tag, checks
  and publication on Ubuntu 26.04. The product's own CI jobs move to the
  same runners.

## 0.5.18 - 2026-09-06

- agent-cli-spec pinned at v2.3.0. The trunk options `--progress
  auto|always|never`, `--verbose` and `--pager auto|always|never` exist on
  every command and in `globalOptions`: diagnostics on stderr in text mode
  only (`maelys_cli_verbose()`, `maelys_cli_detail()`,
  `maelys_cli_progress_wanted()`, `maelys_cli_progress()`,
  `maelys_cli_progress_done()`), silent under `--format json` or `jsonl`;
  the text rendering goes through `PAGER` (POSIX quoting, no shell) or
  `less` with `LESS=FRX` when stdout is a terminal, never in a pipe, in
  JSON or under `--non-interactive`; `--pager` is a rendering option that a
  protocol stream refuses.
- `.pattern` is enforced by the parser as POSIX ERE (`VALIDATION_FAILED`),
  and a pattern that does not compile is refused at startup; the `(?:` group
  of the contract's own `--prefix` grammar is compiled as a plain group. Text records into a pipe are
  tab-separated rows (union of member names sorted by code point, strings
  unquoted and escaped, other values compact JSON); the human line is shown
  on a terminal only. `describe --summary` no longer carries
  `globalOptions`, `invariants` and `output`.
- `python/maelys_cli.py`: the same trunk options, `Invocation.verbose`,
  `progress`, `pager`, `progress_wanted`, `detail()`, `show_progress()`,
  `progress_done()`, `record_text()`, `pager_command()`, `page_text()`;
  `pattern` enforced with `re.search`; `Program` refuses an invalid
  pattern.
- Fix: an external program started by `maelys_cli_process_run()` or
  `maelys_cli_process_replace()` keeps the caller's working directory. Since
  0.5.16 the pathname exec (macOS, and Linux for a script) changed to the
  executable's directory first, so every dispatched `maelys` extension and
  every delegate ran in its own directory and relative operands broke. The
  anchored inode check stays; the exec uses the canonical absolute path.

## 0.5.17 - 2026-09-06

- Security: trusted executables now require an absolute shebang interpreter
  and refuse interpreters named `env`, closing hidden current-directory and
  `PATH` lookups behind an otherwise absolute `execve` call.
- Security: extension `version` and `summary` metadata must be terminal-safe
  single-line UTF-8, preventing ANSI, line and bidirectional controls from
  reaching `maelys help` or `maelys commands list`; Arabic and left-to-right
  or right-to-left marks are covered with the other bidirectional controls.
- These process and extension facilities are C-only and have no Python
  counterpart.

## 0.5.16 - 2026-09-05

- maelys-release v0.14.2 adopted: workflows only (`uses:` lines at 1749a35;
  the generated header no longer carries the literal `\n` of 0.14.0).
- Security: `maelys-cli-embed --define` now performs literal byte
  substitution with `od` and `awk`; replacement text cannot become a `sed`
  program or execute a command. Invalid definition names are refused, with
  an adversarial regression test.
- Security: trusted extension manifests are checked and read through one
  descriptor; `maelys agents install` resolves and writes every managed path
  relative to an open project directory and refuses symbolic-link parents.
  Manifest executable aliases are canonicalized before checking and hashing.
- Security: external execution holds the checked executable and its trusted
  parent open through `exec`; Linux executes binaries by descriptor. Other
  inherited descriptors are marked close-on-exec, preserving the error pipe
  so interpreter and `exec` failures reach the parent as `errno`.
- Text failures, warnings, confirmations and dispatcher startup errors escape
  terminal control bytes originating in arguments or metadata.
- `python/maelys_cli.py`: explicit flag values now accept only the C
  spellings (`true/false`, `yes/no`, `on/off`, `1/0`); malformed values are
  refused instead of enabling transactional `--apply`. Text diagnostics also
  escape terminal control bytes.

## 0.5.15 - 2026-09-05

- maelys-release v0.14.0 adopted: the declarations move from `adapter/` to
  `dependencies/` (`dependencies/agent-cli-spec.pin`,
  `dependencies/maelys-json.pin`, `dependencies/packages`), the layout the
  socle now requires; the Makefile, the documents and the generated files
  follow. maelys-json pinned at v0.1.3 (packaging and test corpus only; the
  library API is unchanged).

## 0.5.14 - 2026-09-05

- agent-cli-spec pinned at v2.2.0: a hidden option. `.hidden` on
  `maelys_cli_option_t` keeps the option out of the derived synopsis, the
  help and the completion while the parser accepts it and `describe` lists
  it with `hidden: true` (emitted only when true); the catalog validation
  refuses a hidden required option. `maelys-hello greet --trace` is the
  example.
- `python/maelys_cli.py`: `hidden=` on `option()` and `flag()`, the same
  behavior; `Program.warn(message)`, the counterpart of `maelys_cli_warn()`.

## 0.5.13 - 2026-09-05

- `python/maelys_cli.py`: `synopsis=` on every declaration function
  overrides the derived usage, as `.synopsis` does in C (must start with
  the pattern; the catalog and `describe` still carry every option). For a
  product whose trial options should not appear on the usage line.
- `python/maelys_cli.py`: its public API is declared stable in
  `docs/python.md` ("Stability of the module"), with the versioning of the
  C library: additive within `0.5`, a `0.6` otherwise, and every change to
  the module named in this changelog under a line starting with the module
  path, for the consumers that vendor the file at `adapter/MAELYS_CLI_PIN`.
  No code change.

## 0.5.12 - 2026-09-05

- `python/maelys_cli.py`: the framework for a product written in Python,
  one file, standard library only, Python 3.9 or later. The declaration
  vocabulary mirrors the C macros (`cli.read`, `cli.records`,
  `cli.transaction`, `cli.execute`, `cli.stream`, `cli.external`;
  `cli.operand`, `cli.option`, `cli.flag`, `cli.argument` with every value
  kind of the contract), with the built-ins, the envelopes, the exit codes,
  the causal order of refusals, `describe --summary --prefix`, the
  completion and `MAELYS_CLI_FORMAT`. `python/examples/hello.py` is its
  reference product, checked by `python/tests/test_maelys_cli.py` and by
  the conformance kit in `make check`. `docs/python.md` is the guide; the
  agent texts mention the module.
- The Python module follows the C library as its reference: `pattern` is
  informative, `MAELYS_CLI_FORMAT` accepts `json` and `text` only, an
  `OSError` maps to the stable code through the table of
  `maelys_cli_file_error_code()` (`file_error_code`, `file_failure`), and
  the file primitives of `files.h` exist with the same requirements,
  errno values and explanations (`open_trusted`, `read_trusted_file`,
  `read_regular_file`, `check_file`, `write_file_atomic`, `zero`,
  `FileError`). `AGENTS.md` and the framework skill require the port in
  every change; `scripts/python-doc-check.sh` holds `docs/python.md` to the
  module; CI runs the module tests on Python 3.9. `python-check` is part of
  `make check` only where `python3` exists, like `contract-check`, runs
  with `-B`, and `__pycache__` is no longer tracked.

## 0.5.11 - 2026-09-05

- `maelys_cli_open_trusted()` and `maelys_cli_read_trusted_file()` judge
  the descriptor they open (`fstat`), so the file checked is the file read;
  the open never blocks (a FIFO is refused as not regular), `O_NOFOLLOW`
  applies under `MAELYS_CLI_FILE_NO_SYMLINK`, and the read is bounded by
  the bytes actually read, never by the size observed before it.
  `maelys_cli_read_regular_file()` is now that read without requirement.
- Requirements `MAELYS_CLI_FILE_SINGLE_LINK` (`EMLINK`) and
  `MAELYS_CLI_FILE_OWNER_CALLER` (`EPERM`), for secrets.
- `maelys_cli_zero()` (zeroing the compiler cannot elide) and buffers zeroed
  before release on every read failure.
- `maelys_cli_file_error_code()` maps an errno to the stable code and
  `maelys_cli_fail_file()` reports it with the explanation as hint.
- Tests exercise every system-call failure of `files.c` through a fault
  hook compiled only into the test copy of the file.

## 0.5.10 - 2026-09-04

- agent-cli-spec pinned at v2.1.0: `describe --summary --prefix PREFIX`
  returns one command namespace with a `filter` member (`INVALID_COMMAND`
  when empty, `VALIDATION_FAILED` on a malformed prefix or a misuse), and an
  option may now conflict with an operand (`.conflicts_with = "COMMAND_ID"`,
  exposed in `conflictsWith` and `input.constraints`), and a string or path
  option may document its regular expression (`.pattern`, exposed as
  `argument.pattern`). Conformance: maelys-hello passes 110 checks and
  maelys 90 with the v2.1.0 kit.

## 0.5.9 - 2026-09-04

- `docs/command-conventions.md` and `docs/agent-cli.md` are reduced to what
  `libmaelys_cli` adds to `agent-cli/v2` (catalog declarations, causal
  parser order, rendering decisions, completion, proof of implementation);
  the contract itself is read in maelys-dev/agent-cli-spec.
- Release socle upgraded to maelys-release 0.6.1.

- The contract `agent-cli/v2` is now specified once, in
  maelys-dev/agent-cli-spec, pinned in `adapter/AGENT_CLI_SPEC_PIN`; its
  conformance kit runs on `maelys-hello` and the `maelys` dispatcher in
  `make check` (`conformance-check`), so the framework is held to the
  written contract like every other implementation. `AGENTS.md` and the
  `maelys-cli-framework` skill tell an agent that a contract change starts
  in the specification, never here. `docs/command-conventions.md`
  and `docs/agent-cli.md` point at the specification and keep what is
  specific to `libmaelys_cli`.

## 0.5.8 - 2026-09-04

- CI aligned on the socle: `ci.yml` calls maelys-release's reusable
  `check-product.yml` at the version `release.yml` pins, so CI and release
  share the same dependency checkouts, packages and checks; the product
  keeps its packaging and GCC jobs.
- Build: maelys-json is compiled inside this tree per build variant
  (`build/<variant>/maelys-json`) with the same flags, instead of the
  sibling's own build directory that a recursive `make` with `BUILD=`
  could not find.

## 0.5.7 - 2026-09-04

- `maelys-cli-reference --neutral-availability [IDS]` describes every
  command, or the identifiers given, as available whatever the host: a
  command `.unavailable` on some hosts (maelys-oci's Linux-only
  `unpack-rootfs`) made the committed contract host-dependent, and the
  product had to normalize it with a script of its own.
- Release socle upgraded to maelys-release 0.5.0: the managed
  `scripts/checkout-dependency.sh maelys-json` replaces the product's
  `scripts/checkout-json.sh`, and the CI drift step calls
  `maelys-release check`.

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
