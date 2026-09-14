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

Hook limits that exist as library constants are declared once with
`MAELYS_CLI_DEFAULT_OF(MAELYS_GIT_HOOK_...)`; the French reference is
generated with `maelys-cli-reference --title --intro-file --columns
--global-label`.

Mirror and pull-request commands whose preconditions go together use
`.group = "preconditions"` on the precondition options and
`.depends_on_all` on `--apply`; hook limits declare their defaults once in
`default_text`; the Cloud-less build declares `run` with `.unavailable`.
The CMake build consumes the framework through `find_package(maelys-cli)`
or FetchContent pinned to a tag, and installs `maelys-cli-reference`
instead of copying the generator.

The envelope, `describe` shape, error codes and exit codes are preserved, so
`tools/generate_cli_reference.py` and the agent documentation keep working.
The only visible additions are `external`, `hidden`, `passthrough`,
`framework` and `cliApi` in `describe`, and richer `argument` metadata.

## Every consumer, at agent-cli-spec 2.3.0 (maelys-cli 0.5.18)

- `.pattern` is now enforced by the parser: a product that declared a
  pattern as documentation must make sure its values match, and rewrite
  any `(?:` group, `\d` class or lookaround into the common subset of
  ECMA-262 and POSIX ERE.
- Text records into a pipe are tab-separated rows; a test that compared the
  `human_line` of `maelys_cli_emit_record()` in a pipe compares the rows
  now (the human line is shown on a terminal only). `jsonl` is unchanged.
- `--progress`, `--verbose` and `--pager` exist on every command; a product
  option with one of these spellings must have the trunk's shape and
  meaning, or be renamed.

## Every consumer, at agent-cli-spec 2.6.0

- A hex or digest operand is now described conformantly: `digits` and
  `algorithms` on an operand, which the framework already emitted and the
  2.5.x schema refused, are what the contract says since 2.6.0. A product
  that avoided a typed operand for that reason may declare it.
- An operand takes `.pattern` as an option does, on a string or path kind,
  compiled at startup and enforced by the parser. A product that checked an
  operand's shape in its handler moves the pattern into the catalog and
  deletes the check.

## Every consumer, at agent-cli-spec 2.5.0

- An all-or-none entry of `input.constraints` no longer carries a `group`
  name; a test that compared the entry compares
  `{"kind":"all-or-none","options":[...]}` now. The name stays on each
  option's `group`, and the kit checks that a command's entries and its
  groups agree, so a `group` without its entry — which cannot happen with
  this framework, the entry being derived — fails the 2.5.0 kit.
- A rule the option fields cannot say is declared on the command:
  `MAELYS_CLI_CONSTRAINTS(array)` of `MAELYS_CLI_CONSTRAINT(kind, options)`
  — `exactly-one` above all, which has no option-level form. A product that
  enforced such a rule in its handler (two of five options given, none
  given) moves the rule into the catalog and deletes the check: `describe`
  states it and the parser refuses it before the handler runs.

## Every consumer, at agent-cli-spec 2.4.0

- `--field NAME` exists on every command; a product option spelled
  `--field` must have the trunk's shape and meaning, or be renamed. No
  other change: a product that never uses `--field` is unaffected.

## Egress (`cli/catalog.c` and `cli/maelys-egress.c`)

Egress reads its configuration and its secrets: since 0.5.11 both go through
`maelys_cli_read_trusted_file()` with the requirements they need
(`MAELYS_CLI_FILE_OWNER_CALLER | MAELYS_CLI_FILE_PRIVATE |
MAELYS_CLI_FILE_NO_SYMLINK | MAELYS_CLI_FILE_SINGLE_LINK` for a secret),
`maelys_cli_zero()` before releasing a secret, and `maelys_cli_fail_file()`
for the error, instead of a `maelys_cli_check_file()` followed by a read and
a product-side errno table.

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
3. delete `tools/generate_cli_reference.py`, `tools/check_cli_contract.py`
   and any local reference-check target: the release socle finds the
   framework's generator by itself at the pinned commit and regenerates and
   compares `docs/cli.md`/`docs/cli-contract.json` (`maelys-release check`,
   `check-product.yml` in CI), declared in `docs/cli.reference` when Egress
   builds outside `build/bin` or documents more than its own binary;
4. replace `docs/command-conventions.md` and `docs/agent-cli.md` with the
   short product templates installed under
   `PREFIX/share/maelys-cli/templates/`, keeping only Egress specifics
   (the lifecycle stream of `serve`, `config validate` as the exit-2
   report) and linking the framework documents;
5. update the shell test and the skill for the new codes and exit semantics;
6. record the contract change in the Egress changelog as a breaking 0.x
   change and bump the minor.

Static archive hygiene: `libmaelys_egress.a` must not absorb
`libmaelys_cli.a` nor `libmaelys-json.a`; its pkg-config file declares
`Requires: maelys-cli` (and `maelys-json` only if Egress reads untrusted
JSON itself), so a product that reuses the Egress library and maelys-json
links each archive once.

Pinning: Egress pins `maelys-cli` by tag through the same
pinned-checkout scheme it uses for `maelys-system`; the framework's CI
(`.github/workflows/ci.yml`) runs the full check on Linux amd64/arm64 and
macOS before a tag is published.

## Hermes

Hermes stays TypeScript; it already implements the same contract. Its
command-contract skill was the model of `share/agents/claude-skill.md`.
`maelys agents install` can be run on Hermes' consumer repositories without
conflict: the block markers differ (`maelys-cli` versus `yavena-hermes`).
