# Working with maelys-cli

This repository is the shared CLI framework of every Maelys tool. Changes
here propagate to Maelys Git, Hermes, Warden and future products, so the
public contract is stricter than in a product repository.

## Constitution

- No product type enters `include/` or `src/`: no policy, MIR, receipt,
  repository, editorial or OCI concept. Product semantics live in product
  handlers.
- The core (`libmaelys_cli.a`) has no dependency: `src/json.c` writes and
  syntax-checks CLI output, nothing more. Reading untrusted JSON is
  maelys-json's job, confined to `libmaelys_cli_extension.a`; do not
  reimplement UTF-8 validation, duplicate-key detection, limits or a
  document model in the core, and do not link maelys-json into it.
- Static archives never embed their dependencies; the build system
  (pkg-config `Requires`, CMake `PUBLIC` links) resolves one copy per
  executable.
- Linux and macOS must expose the same observable behavior.
- Every primitive ships with positive and adversarial tests in `tests/`.
- Public headers compile as C11 and C++17 (`tests/header_cpp.cpp`).

## Licensing boundary

Code is MPL-2.0; `share/agents/` and `share/templates/` are CC0-1.0 because
they are copied into consumer repositories. Never move code into `share/`
or licensed text into the agent texts. See `LICENSING.md`.

## Contracts that must not drift

- `agent-cli/v2` is specified outside this repository, in
  [maelys-dev/agent-cli-spec](https://github.com/maelys-dev/agent-cli-spec)
  (`spec/agent-cli.md`, the schemas, the conformance kit), pinned in
  `adapter/AGENT_CLI_SPEC_PIN`. This framework is one implementation of it,
  next to Hermes and maelys-release, and is held to it by
  `make conformance-check` (part of `make check`) on `maelys-hello` and the
  `maelys` dispatcher. Never let the kit fail; never change the envelopes,
  the `describe` shape, the error codes or the exit codes here first: a
  contract change is a pull request on agent-cli-spec, a new tag there, then
  a pin bump here, then the code. Where `docs/command-conventions.md` and
  the specification differ, the specification wins and the document is
  corrected.
- The success and failure envelopes, error codes and exit codes are the
  `agent-cli/v2` contract shared with Maelys Git and Hermes. Changing them
  incompatibly requires bumping `MAELYS_CLI_CONTRACT` and
  `MAELYS_CLI_SCHEMA_VERSION` together, after the specification moved to
  `agent-cli/v3`.
- The `describe` shape (`id`, `pattern`, `usage`, `purpose`, `effect`,
  `outputMode`, `input`, `outputSchema`, `exitCodes`, `globalOptions`) feeds
  reference generators and agents. Additive changes only within a contract
  version.
- The extension manifest `maelys.cli-extension/v1` and `MAELYS_CLI_API`
  change together.
- `maelys_cli_command_t` and `maelys_cli_option_t` are declared through
  the `MAELYS_CLI_*` macros and designated initializers. Append new fields at
  the end, keep zero as the neutral value and extend the macros rather than
  their argument lists; see `docs/abi.md`.
- `tools/maelys-cli-embed` is installed for consumers; keep it POSIX `sh`
  with `od` and `awk` only.
- `python/maelys_cli.py` is the Python counterpart of the C library and
  the C library is the reference: same declaration vocabulary, same causal
  order of refusals, same `describe` shape, same errno-to-code table, same
  file primitives with the same requirements and explanations, same
  `MAELYS_CLI_FORMAT` values, `argument.pattern` informative in both. A
  behavior that exists in one and not the other is a defect. It is held by
  `python/tests/test_maelys_cli.py`, by the conformance kit on
  `python/examples/hello.py` and by `scripts/python-doc-check.sh`
  (`python-doc-check`: every public name documented in `docs/python.md`);
  CI runs its tests on Python 3.9, the oldest interpreter it declares.
  Never track `__pycache__`; every check runs Python with `-B`.

## Doctrine enforced by the framework

- stdout carries success data only; stderr carries diagnostics and failure
  envelopes; a `protocol-stream` command reserves stdout completely and
  rejects rendering flags;
- unknown, duplicated (unless repeatable) or foreign options are refused;
- validation errors are reported in causal order: command, option spelling
  and duplicates, option values, dependencies and conflicts, required
  options, operand arity, rendering constraints;
- transactional commands plan by default and write with `--apply`;
  `--dry-run` and `--plan` are refused with the migration hint;
- `--non-interactive` guarantees that no prompt is ever shown;
- every file write names `MAELYS_CLI_WRITE_REPLACE` or
  `MAELYS_CLI_WRITE_NO_REPLACE`;
- external programs run from absolute, trusted paths through `execve`,
  never a shell or PATH lookup.

## Adding or changing a framework feature

The procedure below is what every release since 0.2 followed. The Claude
skill `.claude/skills/maelys-cli-framework/SKILL.md` is its checklist form.

1. Classify the change: **mechanic** (`values`, `files`, `process`, ...),
   **catalog** (a descriptor field, kind or macro), **runtime** (parser,
   rendering, built-in command) or **contract** (envelope, `describe`
   shape, exit codes, manifest). A contract change is additive or it bumps
   `MAELYS_CLI_CONTRACT`/`MAELYS_CLI_SCHEMA_VERSION` (or `MAELYS_CLI_API`).
2. Public surface: declare it in the matching `include/maelys/cli/*.h`
   with a contract comment. New descriptor fields go at the end of the
   struct with zero as the neutral value; add or extend a `MAELYS_CLI_*`
   macro, never a macro argument. Anything a product could need in a
   handler is an accessor on `maelys_cli_context_t`, never a private global.
   Then port the same surface to `python/maelys_cli.py` in the same change
   (a declaration keyword, a value kind, a file primitive, an error
   mapping), with the C behavior as the reference; a change that has no
   Python counterpart says so in the changelog.
3. Catalog validation: `maelys_cli_catalog_validate()` refuses every
   inconsistent declaration of the new field at startup, naming the
   command and option. A default, a reference to another option, a limit
   or a length that the framework can check must be checked there.
4. Parser: enforce the new rule in `maelys_cli_parse()` at its causal
   position and with a message that names the option or operand and the
   expected shape; validate operands and options through the same
   value validator.
5. `describe`: expose the new information additively (per option, per
   operand or in `input.constraints`); keep single-command `describe`
   minimal. Update `__complete` when the feature changes what a shell can
   offer.
6. Tests, in the same change: unit test in `tests/test_*.c` (accepted and
   refused cases, `describe` output, catalog validation refusals), and
   `tests/test_cli.sh` when the behavior is visible from a binary; the
   same cases in `python/tests/test_maelys_cli.py` for the Python port.
7. Documentation, in the same change: `docs/api-reference.md` (enforced
   by `scripts/api-doc-check.sh`), the installed agent texts in
   `share/agents/` (enforced by `scripts/agent-doc-check.sh` for macros
   and accessors), `docs/command-conventions.md`, `docs/agent-cli.md`,
   `docs/architecture.md`, `share/templates/` and `README.md` where the
   feature is user-visible; `docs/python.md` for the Python port (enforced
   by `scripts/python-doc-check.sh`). Then add the feature's keyword to every
   document that must explain it in `docs/topics.tsv`: `make check` runs
   `scripts/doc-topics-check.sh` (`doc-topics-check`) and fails while a
   listed document does not mention a listed keyword. A programmatic doc
   patch whose anchor no longer exists fails silently; the topic check is
   what catches it.
8. Release bookkeeping: `CHANGELOG.md` entry under a new version, `VERSION`
   and `include/maelys/cli/version.h` bumped together (minor for an
   additive API, patch for documentation or fixes), then
   `make generate-cli-reference` so `contract-check` passes.
9. Verification before pushing: `make check` (unit, end-to-end, header
   gate, version, API docs, agent docs, contract drift), `make asan-ubsan`,
   `make analyze`, `make install-check`, `make cmake-check`. Linux and GCC
   are verified by CI; Docker `gcc:14` reproduces most of it locally but
   not Ubuntu's fortified `warn_unused_result`.
10. Tag only a commit whose CI is green (`git tag -a vX.Y.Z`, push the
    tag); never move a pushed tag. Consumers pin tags.

## Behaviors that look like bugs but are decisions

- `--dry-run`/`--plan` are refused only on commands declaring `--apply`.
- `maelys_cli_json_string(NULL)` fails the writer; `null` is explicit.
  Invalid UTF-8 fails it too: products transcode or escape raw names.
- The JSON validator rejects duplicate keys; a manifest with two
  `executable` members is invalid, not "last wins".
- Stream commands refuse rendering options; `MAELYS_CLI_FORMAT` in the
  environment is the way to shape their failure envelope.
- `describe COMMAND_ID` omits `globalOptions`, `output` and `invariants`.
- A single invalid extension manifest stops the `maelys` dispatcher.
- The reference generator omits versions unless `--include-versions`.

## When adding a command to `maelys` or `maelys-hello`

Follow the checklist installed in consumer projects:
`share/agents/instructions-block.md` and `share/agents/maelys-cli-guide.md`.
They are the same rules this repository applies to itself: one catalog
entry, one handler replying exactly once, one output schema, focused tests,
and no hand-written usage text.

<!-- maelys-release:begin -->
# Maelys release socle (maelys-release)

This repository publishes through the shared maelys-release workflows. The
rules below hold for every release-related change; the complete conventions
are in `docs/conventions.md` of maelys-release.

- `.github/workflows/release.yml` and `scripts/checkout-dependency.sh` are
  generated by `bin/maelys-release adopt` of maelys-release from
  `adapter/*_PIN`, `adapter/PACKAGES` and `packaging/homebrew/*.rb.in`.
  Never edit them by hand; change the declarations, then run
  `maelys-release adopt DIR --apply` from a maelys-release checkout at the
  wanted tag. `maelys-release check DIR` (exit 2 on any violation) verifies;
  `maelys-release preflight DIR` checks the tag preconditions before a
  release. The command follows agent-cli/v2: `--format json` everywhere,
  `describe` for the catalog.
- A dependency on another Maelys repository is `adapter/<NAME>_PIN` (tag on
  line 1, commit on line 2), cloned by `scripts/checkout-dependency.sh NAME`.
  The packages the build needs on the runners are listed in
  `adapter/PACKAGES` under `[linux]` and `[macos]`; no script installs them.
  `.github/workflows/ci.yml` calls the socle's `check-product.yml`, which
  reads these declarations itself; `adopt` keeps that line's socle version
  current and `check` warns when no job calls it. `maelys-release rehearse
  DIR TARGET` replays the Linux build job in Docker before a first tag.
- A release is a signed, annotated tag `vX.Y.Z` on `main` whose commit
  carries `VERSION` = `X.Y.Z` and a dated `CHANGELOG.md` entry. Never push a
  tag before `make check` passes on that exact commit, never move or force a
  tag, never publish from a branch.
- The workflow verifies the tag through the GitHub API, builds on Linux
  x86_64, Linux arm64 and macOS arm64 with `scripts/package-release.sh
  TARGET`, attests provenance, publishes the GitHub release, renders
  packaging/homebrew/libmaelys-cli.rb.in, packaging/homebrew/maelys.rb.in from the tag's own copy, builds
  bottles when configured and pushes the formula to `maelys-dev/homebrew-tap`.
- Formula names: a command is named after its binary (`maelys-egress`), a
  library after its archive with a `lib` prefix (`libmaelys-sys`). Dependency
  pins in a formula are copied from the tag's `adapter/` files, never typed.
- Tap credentials are the repository secrets `HOMEBREW_TAP_TOKEN` and
  `HOMEBREW_TAP_SIGNING_KEY`; without them the tap job renders, lints and
  reports instead of failing. Never commit a secret or a key.
- Runners are JSON inputs of the workflow; public repositories use
  GitHub-hosted runners only. A self-hosted runner is reserved for hardware
  gates, on signed tags or `workflow_dispatch`, behind the `release`
  environment.
- A tag whose release exists but whose formula or bottles failed is
  replayed with `gh workflow run release.yml -f tag=vX.Y.Z` after adopting
  a corrected socle; a tag is never moved or recreated.
<!-- maelys-release:end -->
