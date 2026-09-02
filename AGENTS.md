# Working with maelys-cli

This repository is the shared CLI framework of every Maelys tool. Changes
here propagate to Maelys Git, Hermes, Warden and future products, so the
public contract is stricter than in a product repository.

## Constitution

- No product type enters `include/` or `src/`: no policy, MIR, receipt,
  repository, editorial or OCI concept. Product semantics live in product
  handlers.
- No external dependency. JSON is handled by `src/json.c`; do not add
  Jansson or any library to the core.
- Linux and macOS must expose the same observable behavior.
- Every primitive ships with positive and adversarial tests in `tests/`.
- Public headers compile as C11 and C++17 (`tests/header_cpp.cpp`).

## Licensing boundary

Code is MPL-2.0; `share/agents/` and `share/templates/` are CC0-1.0 because
they are copied into consumer repositories. Never move code into `share/`
or licensed text into the agent texts. See `LICENSING.md`.

## Contracts that must not drift

- The success and failure envelopes, error codes and exit codes are the
  `agent-cli/v2` contract shared with Maelys Git and Hermes. Changing them
  incompatibly requires bumping `MAELYS_CLI_CONTRACT` and
  `MAELYS_CLI_SCHEMA_VERSION` together and updating
  `docs/command-conventions.md`.
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
   `tests/test_cli.sh` when the behavior is visible from a binary.
7. Documentation, in the same change: `docs/api-reference.md` (enforced
   by `scripts/api-doc-check.sh`), the installed agent texts in
   `share/agents/` (enforced by `scripts/agent-doc-check.sh` for macros
   and accessors), `docs/command-conventions.md`, `docs/agent-cli.md`,
   `docs/architecture.md`, `share/templates/` and `README.md` where the
   feature is user-visible. After patching a document programmatically,
   grep for the feature name in every agent-facing file: a replacement
   whose anchor no longer exists fails silently.
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
