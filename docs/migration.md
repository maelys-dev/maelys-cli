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

The descriptor types are the same concept with more kinds. `OID` and
`SHA256` become `HEX` with `hex_digits` 40/64 (or a `STRING` validated in
the handler for dual-length OIDs). `program` disappears: each binary
declares its own `maelys_cli_app_t` with its subset of commands. `synopsis`
may stay as an explicit override during the migration and then be dropped
in favor of the derived form. `output_schema_json` keeps the same strings.

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

## Hermes

Hermes stays TypeScript; it already implements the same contract. Its
command-contract skill was the model of `share/agents/claude-skill.md`.
`maelys agents install` can be run on Hermes' consumer repositories without
conflict: the block markers differ (`maelys-cli` versus `yavena-hermes`).
