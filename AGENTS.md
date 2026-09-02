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

## When changing the framework

1. Update the header, the implementation, `docs/api-reference.md` (checked
   by `scripts/api-doc-check.sh`), the focused unit test and the end-to-end
   script `tests/test_cli.sh` in the same change.
2. If the change affects `describe`, `help` or the envelope, update
   `docs/command-conventions.md`, `docs/agent-cli.md`, the agent texts in
   `share/agents/` and regenerate `docs/cli-reference.md` with
   `make generate-cli-reference`.
3. Run `make check`, then `make asan-ubsan`.
4. Record the change in `CHANGELOG.md`.

## When adding a command to `maelys` or `maelys-hello`

Follow the checklist installed in consumer projects:
`share/agents/instructions-block.md` and `share/agents/maelys-cli-guide.md`.
They are the same rules this repository applies to itself: one catalog
entry, one handler replying exactly once, one output schema, focused tests,
and no hand-written usage text.
