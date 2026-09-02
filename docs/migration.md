# Migrating existing Maelys CLIs

## Warden (`cli/common` and `cli/maelys-warden.c`)

`cli/common.h` maps one-to-one onto `maelys/cli/values.h`,
`maelys/cli/environment.h` and `maelys/cli/files.h`:

| Warden | maelys-cli |
| --- | --- |
| `maelys_cli_parse_u64_decimal` | same name and contract |
| `maelys_cli_parse_u32_decimal` | same |
| `maelys_cli_parse_byte_size` | same, plus `T` suffix |
| `maelys_cli_string_list_*` | same |
| `maelys_cli_environment_*` | same, plus `get` and `to_envp` |
| `maelys_cli_read_regular_file` | same |
| `maelys_cli_write_file_atomic` | same; `NO_REPLACE` also refuses an existing symlink |

The hand-written `run` parser becomes one catalog entry. Its option table
already lists every option with a kind: `--vm-cpus` is `UNSIGNED`,
`--vm-memory` is `SIZE`, `--wall-time` becomes `DURATION` (`600s`) or
stays `UNSIGNED` milliseconds during the transition, `--network-frontend`
is a `CHOICE` of `auto|fd4|proxy`, `--env` is a repeatable `STRING`,
`--allow-read` a repeatable `PATH`. Mutual exclusion of `--ro`, `--rw` and
`--rootfs-only` is expressed with `conflicts_with`; the remaining semantic
checks (OCI requires `v9`, resource budgets require `--image`) stay in the
handler with `PRECONDITION_FAILED` or `VALIDATION_FAILED`. `run` is a
`stream` command: the workload owns stdout and the exit code.

`maelys-warden image ...` becomes a delegate entry
(`.delegate = "maelys-warden-oci-materializer"` plus a dedicated `pull`
entry) until `maelys oci` replaces it. Warden's search order (`beside`,
`../libexec`, Homebrew and system libexec) is expressed through
`helper_directories`; the `MAELYS_OCI_MATERIALIZER` override becomes an
absolute delegate chosen by the product before calling `maelys_cli_main()`.

## Maelys Git (`src/cli/catalog.c` and `src/cli/cli.c`)

The descriptor types are the same concept with more kinds. `OID` becomes
`MAELYS_CLI_HEX_OR(name, "OID", summary, 40u, 64u)` and `SHA256` becomes
`MAELYS_CLI_HEX(name, "SHA256", summary, 64u)`. The per-protocol output
modes (`git-smart-protocol-stream`, `git-hook-stream`,
`maelys-git-agent-jsonl-stream`, `maelys-git-events-jsonl-stream`) become
`MAELYS_CLI_PROTOCOL_STREAM(..., "git-smart")` and so on: `describe` reports
`outputMode: "protocol-stream"` plus `protocol`. `program` disappears: each
binary declares its own `maelys_cli_app_t` with its subset of commands and
`tools/generate_cli_reference.py BIN...` aggregates them. `synopsis` may
stay as an explicit override during the migration and then be dropped in
favor of the derived form. `output_schema_json` keeps the same strings, or
moves to JSON Schema files embedded with `maelys-cli-embed`.

`maelys_cli_emit_success(invocation, json_t *data, human, exit_code)`
becomes `maelys_cli_succeed(context, json_dumps(data), human, exit_code)`:
the product keeps Jansson for its data and hands serialized text to the
framework. `maelys_cli_emit_maelys_error()` becomes a product helper mapping
`maelys_git_result_t` to `maelys_cli_fail()` codes; the table in
`docs/command-conventions.md` is unchanged.

The envelope, `describe` shape, error codes and exit codes are preserved, so
`tools/generate_cli_reference.py` and the agent documentation keep working.
The only visible additions are `external`, `hidden`, `passthrough`,
`framework` and `cliApi` in `describe`, and richer `argument` metadata.

## Egress (`cli/catalog.c` and `cli/maelys-egress.c`)

Egress is the simplest consumer and the one whose migration is a contract
change rather than a code change: its 0.11 CLI reimplemented the same model
under the same `agent-cli/v2` name with a different vocabulary. The
reference vocabulary is the one of Maelys Git and Hermes, which `maelys-cli`
implements; Egress aligns on it in its next minor.

| Egress 0.11 | `maelys-cli` | Note |
| --- | --- | --- |
| `describe` member `path` | `pattern` (array of words) | `id` is added: dotted, stable |
| `usage` | `usage` and `input.synopsis` | identical strings, derived from the catalog |
| `summary` | `purpose` | operands and options keep `summary` |
| `effect`, `outputMode` | same names, same values | `protocol` added for stream commands |
| no `input`, no `outputSchema` | `input.operands`, `input.options`, `input.constraints`, `outputSchema`, `exitCodes` | additive |
| error codes in kebab case (`invalid-argument`) | `VALIDATION_FAILED`, `NOT_FOUND`, ... | the eleven stable codes of `command-conventions.md` |
| exit `2` = invalid invocation | exit `1` = any failure, `2` = a validation report with violations | `2` never accompanies an error envelope |

Steps, in one Egress change:

1. rewrite `cli/catalog.c` with the `MAELYS_CLI_*` macros and move each
   output schema to `schemas/*.json` embedded by `maelys-cli-embed`;
2. replace the hand-written parser and renderer of `cli/maelys-egress.c`
   with `maelys_cli_main()`; `serve` becomes
   `MAELYS_CLI_PROTOCOL_STREAM(..., "egress-fd4")` or the relevant name;
3. delete `tools/generate_cli_reference.py` and `tools/check_cli_contract.py`
   in favor of the framework's generator (`generate_cli_reference.py
   --build DIR maelys-egress`) and of `describe` itself;
4. shorten `docs/command-conventions.md` and `docs/agent-cli.md` to Egress
   specifics and link the framework documents installed under
   `PREFIX/share/maelys-cli/docs/`;
5. update the shell test and the skill for the new codes and exit semantics;
6. record the contract change in the Egress changelog as a breaking 0.x
   change and bump the minor.

Pinning: Egress pins `maelys-cli` by tag (`v0.1.0`) through the same
pinned-checkout scheme it uses for `maelys-system`; the framework's CI
(`.github/workflows/ci.yml`) runs the full check on Linux amd64/arm64 and
macOS before a tag is published.

## Hermes

Hermes stays TypeScript; it already implements the same contract. Its
command-contract skill was the model of `share/agents/claude-skill.md`.
`maelys agents install` can be run on Hermes' consumer repositories without
conflict: the block markers differ (`maelys-cli` versus `yavena-hermes`).
