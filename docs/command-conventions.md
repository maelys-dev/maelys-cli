# Maelys command conventions

This document is normative for every command of a CLI built on
`libmaelys_cli`. The catalog (`maelys_cli_command_t[]`) is the executable
source of truth: the parser, `help`, `describe`, the tests and any generated
reference depend on it. Nothing may maintain a second usage string.

## Discovery

An agent starts with:

```sh
PROGRAM describe --summary --format json --compact --non-interactive
PROGRAM describe COMMAND_ID --format json --compact --non-interactive
```

Each descriptor exposes a stable `id`, its `pattern`, `usage` (identical to
`input.synopsis`), `purpose`, `effect`, `outputMode`, `external`, `hidden`,
`input` (`operands`, typed `options`, `constraints`, `passthrough`),
`outputSchema` and `exitCodes`.

`describe COMMAND_ID` returns one minimal descriptor; `globalOptions`,
`output` and `invariants` belong to the inventory forms only.

Operands may be typed like option values (`MAELYS_CLI_OPERAND_CHOICE`,
`MAELYS_CLI_OPERAND_KIND` plus limits); the parser validates them, `describe`
exposes `type`, `choices` and limits on the operand, and handlers read them
through `maelys_cli_operand_choice()`, `maelys_cli_operand_unsigned()` and
`maelys_cli_operand_integer()`.

Option constraints: `depends_on` (one option), `depends_on_all` (every
listed option), `conflicts_with`, and `group` (all-or-none: the options of
a group are given together or not at all). `describe` exposes them as
`requires`, `conflictsWith`, `group` and `input.constraints` entries of kind
`requires`, `at-most-one` and `all-or-none`.

`default_text` is the single source of an option's default: the catalog
validation checks it against the option's kind, and the typed accessors
return it when the option is absent. A command that a build cannot provide
declares `.unavailable = "reason"` instead of a failing handler; it stays in
`describe` with `available: false` and fails with `UNSUPPORTED`. A product
with build variants composes its catalog at startup with
`maelys_cli_catalog_concat()`: a later part that declares the same
identifier replaces the `.unavailable` descriptor in place, so the extended
build provides the command where the base build describes it; shadowing a
command that is already provided is refused (`EEXIST`).

A public option must be declared in the catalog before use. An unknown,
duplicated (unless `repeatable`) or foreign option is refused. An option
value remains a value even when it starts with `--`.

## Effects

| Effect | Meaning | Durable write |
| --- | --- | --- |
| `read` | Inspects or validates state. | No |
| `preview` | Builds an exact plan. | No |
| `apply` | Applies a reviewed transaction. | Yes |
| `commit` | Records a reviewed version-control commit. | Yes |
| `execute` | Deliberately runs a non-transactional action. | Per action |
| `stream` | Reserves stdio for a declared protocol. | Per protocol |

A transactional command declares `effect = preview` and `apply_effect =
apply` (or `commit`) and exposes `{"plan":"preview","apply":"apply"}`.
Without `--apply` it returns `mode: "plan"` and writes nothing. With
`--apply` it re-validates its preconditions and returns `mode: "apply"`.
`--dry-run` and `--plan` are refused with `VALIDATION_FAILED` and the
migration hint, only on commands that declare `--apply`; a product without
transactions is not affected. The refusal is deliberate: accepting a
`--dry-run` alias would let two spellings of the same intent coexist across
products, which is exactly what the shared contract exists to prevent.

## Inputs

Value kinds: `boolean` (flag, `--flag=false` accepted), `string`,
`integer`, `unsigned`, `size` (K/M/G/T), `duration` (unit required: ms, s,
m, h, d; delivered in milliseconds), `path` (non-empty), `absolute-path`
(starts with `/`), `choice`, `hex`, `digest` (`ALGORITHM:HEX` with the
algorithm among the declared `algorithms` and the length implied by it). Ranges,
choices and digit counts are declared in the catalog and enforced by the
parser before the handler runs. A `hex` option may accept two lengths
(`MAELYS_CLI_HEX_OR`, for SHA-1 or SHA-256 object identifiers).

Errors are reported in this causal order:

1. command resolution (`INVALID_COMMAND`);
2. option spelling, support by the command, duplication;
3. option value kind, range, choice;
4. option dependencies (`depends_on`) and conflicts (`conflicts_with`);
5. required options;
6. operand arity;
7. rendering constraints (stream commands, jsonl availability);
8. inside the handler: file type and permissions, syntax, schema, policy,
   current state and concurrency preconditions.

`--` ends option parsing; every following argument is an operand. Delegate
commands receive every argument after their pattern verbatim, including
`--help`.

Administrative paths are absolute when they become durable configuration.
Secrets are regular non-symlink files owned by the caller with no group or
world permissions (`maelys_cli_check_file()` with `MAELYS_CLI_FILE_PRIVATE`).
An existing write target is never replaced implicitly.

## Outputs

`--json` is the exact alias of `--format json`. `--compact` and
`--pretty=false` select a single line. `--format jsonl` is accepted only by
`json-records` commands. `MAELYS_CLI_FORMAT=json` in the environment selects
JSON when no rendering option is given, which is how an agent obtains a JSON
failure envelope from a `stream` command whose stdout it cannot touch. `--non-interactive` guarantees that no question is
asked; `maelys_cli_confirm()` fails with `VALIDATION_FAILED` instead.

Success (stdout only):

```json
{
  "schemaVersion": 2,
  "contract": "agent-cli/v2",
  "command": "note.write",
  "ok": true,
  "exitCode": 0,
  "data": {}
}
```

Failure (stderr only):

```json
{
  "schemaVersion": 2,
  "contract": "agent-cli/v2",
  "command": "note.write",
  "ok": false,
  "exitCode": 1,
  "error": {
    "code": "VALIDATION_FAILED",
    "message": "Stable causal diagnostic.",
    "hint": "Next safe action."
  }
}
```

Process exit codes: `0` completed, `1` execution failure, `2` a validation
correctly executed that found violations. A negative authorization decision
is a successful read (`data.allowed: false`), never exit `1`. A `stream`
command or a delegate propagates the exit status of the underlying process
(`128 + signal` on signal termination).

Stable error codes:

| Code | Boundary |
| --- | --- |
| `INVALID_COMMAND` | Command not found. |
| `VALIDATION_FAILED` | Input shape, option, type or limit invalid. |
| `PRECONDITION_FAILED` | State changed or does not allow the transaction. |
| `POLICY_FAILED` | Policy could not be loaded or evaluated. |
| `ACCESS_DENIED` | Negative security decision, untrusted file or binary. |
| `NOT_FOUND` | Required resource absent. |
| `IO_FAILED` | System read or write failed. |
| `PROCESS_FAILED` | Subprocess or external command failed. |
| `PROTOCOL_FAILED` | Message or manifest violates its protocol. |
| `UNSUPPORTED` | Function absent from this build or version. |
| `UNEXPECTED` | Unclassified defect to fix in the implementation. |

Text rendering of a failure is `PROGRAM: [CODE] message` followed by
`Hint: ...` when present, colored on a terminal unless `--color never`,
`NO_COLOR` or `TERM=dumb` applies.

## Shell completion

`PROGRAM completion bash|zsh|fish` prints a shim that delegates to the
hidden `PROGRAM __complete -- WORDS...` command. Candidates are derived from
the catalog at every keystroke: command words, options not yet given,
`--option=choice`, choice values, digest algorithm prefixes and typed
operands, and command identifiers after `help` and `describe`. Path and
free-text values fall back to the shell's file completion. Stream commands
never offer rendering options; unavailable commands are never offered. A
dispatcher forwards the completion of an external command to that
command's own `__complete`.

## Protocol streams

`stream` commands and delegates are the only exceptions to the envelope.
They refuse rendering options, keep diagnostics on stderr and never inject
banners, progress or JSON into their stdout. A stream command names the
protocol that owns its stdio through `.protocol` (for example `git-smart`,
`git-hook`, `maelys-git-agent/1`), exposed as `protocol` next to
`outputMode: "protocol-stream"` in `describe`.

## Proof of implementation

Any command change updates, in the same change:

1. the catalog entry, declared with the `MAELYS_CLI_*` macros;
2. the handler consuming the validated invocation;
3. the JSON Schema file when the data shape changes (embedded with
   `maelys-cli-embed`, referenced with `MAELYS_CLI_SCHEMA`);
4. the tests of the catalog, options and envelopes;
5. the generated reference.

`maelys_cli_catalog_validate()` runs at every startup and in tests. It
checks identifier and pattern validity and uniqueness, summaries, value
declarations, `depends_on`/`conflicts_with` targets, schema JSON validity, the
presence of `--apply` on transactions and the handler-or-delegate rule.
