# libmaelys_cli implementation notes

The contract every command follows is `agent-cli/v2`, specified in
[maelys-dev/agent-cli-spec](https://github.com/maelys-dev/agent-cli-spec)
(`spec/agent-cli.md` and `spec/extensions.md`, pinned in
`adapter/AGENT_CLI_SPEC_PIN`). The specification defines discovery
(`describe` in its three forms), the descriptor, the value kinds, the six
effects and the plan/`--apply` transaction, the global options, the
built-in commands, the envelopes, the exit codes and the stable error
codes, protocol streams and delegates. Its conformance kit runs on
`maelys-hello` and the `maelys` dispatcher in `make check`. Where this
document and the specification differ, the specification wins and this
document is corrected.

This document only records how `libmaelys_cli` implements that contract:
what the catalog declares, what the parser enforces before a handler runs,
and what a change must prove. It is normative for products built on the
framework.

## The catalog is the single source

The catalog (`maelys_cli_command_t[]`, declared with the `MAELYS_CLI_*`
macros) is the executable source of truth: the parser, `help`, `describe`,
completion, the tests and any generated reference depend on it. Nothing
may maintain a second usage string; `usage` and `input.synopsis` are
derived from the declaration (required options first, then optional ones,
each in declaration order; choices without a `value_name` render as `a|b`).

Value kinds map one to one onto the specification: `MAELYS_CLI_FLAG`
(`boolean`), `_STRING`, `_INTEGER`, `_UNSIGNED`, `_SIZE`, `_DURATION`,
`_PATH`, `_ABSOLUTE_PATH`, `_CHOICE`, `_HEX` and `MAELYS_CLI_HEX_OR` (`hex`, one or
two accepted lengths), `_DIGEST` (`ALGORITHM:HEX`). Ranges, choices and digit
counts are enforced by the parser; a handler never re-validates a shape a
kind expresses.

Operands may be typed like option values (`MAELYS_CLI_OPERAND_CHOICE`,
`MAELYS_CLI_OPERAND_KIND`, or `MAELYS_CLI_OPERAND_OPTIONAL` plus `.kind`);
handlers read them through `maelys_cli_operand_choice()`,
`maelys_cli_operand_unsigned()` and `maelys_cli_operand_integer()`.

Option constraints: `depends_on` (one option), `depends_on_all` (every
listed option), `conflicts_with` (an option, or an operand named by its
UPPER_CASE placeholder, as `--prefix` conflicts with `COMMAND_ID`), and
`group` (all-or-none). They are exposed as `requires`, `conflictsWith`,
`group` and `input.constraints` entries of kind `requires`, `at-most-one`
and `all-or-none`.

`.pattern` documents, as `argument.pattern`, the regular expression a
string or path option must match; the framework exposes it and the handler
enforces it (no regex engine runs in the parser).

`default_text` is the single source of an option's default: the catalog
validation checks it against the option's kind at startup, and the typed
accessors return it when the option is absent; `MAELYS_CLI_DEFAULT_OF`
takes it from a constant of the product library.

A command that a build cannot provide declares `.unavailable = "reason"`
instead of a failing handler: it stays in `describe` with
`available: false` and fails with `UNSUPPORTED`. A product with build
variants composes its catalog at startup with
`maelys_cli_catalog_concat()`: a later part may replace an `.unavailable`
descriptor of the same identifier in place, and nothing else (`EEXIST`).

A stream command names the protocol that owns its stdio through
`.protocol` (`MAELYS_CLI_PROTOCOL_STREAM`), exposed as `protocol` next to
`outputMode: "protocol-stream"`; a delegate (`MAELYS_CLI_EXTERNAL`) relays
a child without a named protocol and has no `protocol` member.

## What the parser enforces, in causal order

1. command resolution (`INVALID_COMMAND`);
2. option spelling, support by the command, duplication (unless
   `repeatable`);
3. option value kind, range, choice;
4. option dependencies (`depends_on`, `depends_on_all`, `group`) and
   conflicts (`conflicts_with`);
5. required options;
6. operand arity and typed operands;
7. rendering constraints: stream commands refuse rendering options, `jsonl`
   is accepted only by `json-records` commands.

Everything after that belongs to the handler: file type and permissions,
syntax, schema, policy, current state and concurrency preconditions,
reported with the error code of the boundary that actually failed.
Configuration, manifests and secrets are read with
`maelys_cli_read_trusted_file()`, which applies the trust requirements to
the descriptor it reads and bounds the read by the bytes read, so a link
posted between a check and a read, a file that grows meanwhile or a FIFO at
the path cannot change what is judged; `maelys_cli_check_file()` remains for
a file that is not read, such as an executable. `maelys_cli_fail_file()`
turns the errno and explanation they leave into the stable code:
`NOT_FOUND`, `ACCESS_DENIED`, `VALIDATION_FAILED` or `IO_FAILED`.

An option value remains a value even when it starts with `--`; `--` ends
option parsing; delegate commands receive every argument after their pattern
verbatim, including `--help`.

`--dry-run` and `--plan` are refused, with the migration hint, only on
commands that declare `--apply`; a product without transactions is not
affected. The refusal is deliberate: accepting an alias would let two
spellings of the same intent coexist across products, which is what the
shared contract exists to prevent.

## Rendering decisions

`MAELYS_CLI_FORMAT=json|text` in the environment selects the default
rendering when no rendering option is given; for a stream command it only
shapes the failure envelope on stderr, since stdout belongs to the protocol.
`--non-interactive` guarantees that no question is asked:
`maelys_cli_confirm()` fails with `VALIDATION_FAILED` instead.

Text rendering of a failure is `PROGRAM: [CODE] message` followed by
`Hint: ...` when present, colored on a terminal unless `--color never`,
`NO_COLOR` or `TERM=dumb` applies. Envelope keys are written in a fixed
order (`schemaVersion`, `contract`, `command`, `ok`, `exitCode`, then
`data` or `error`), and `describe COMMAND_ID` is minimal: `globalOptions`,
`output` and `invariants` belong to the inventory forms only.

`ACCESS_DENIED` also covers an untrusted file or binary (ownership, modes,
symlink, digest), and `PROTOCOL_FAILED` a manifest that violates
`maelys.cli-extension/v1`. `UNEXPECTED` is what a handler that never
replies, or replies with invalid JSON, produces.

Administrative paths are absolute when they become durable configuration.
Secrets are regular non-symlink files owned by the caller with no group or
world permissions (`MAELYS_CLI_FILE_PRIVATE`). Every file write names
`MAELYS_CLI_WRITE_REPLACE` or `MAELYS_CLI_WRITE_NO_REPLACE`; an existing
target is never replaced implicitly.

## Shell completion

`PROGRAM completion bash|zsh|fish` prints a shim that delegates to the
hidden `PROGRAM __complete -- WORDS...` command. Candidates are derived from
the catalog at every keystroke: command words, options not yet given,
`--option=choice`, choice values, digest algorithm prefixes, typed operands,
and command identifiers after `help` and `describe`. Path and free-text
values fall back to the shell's file completion. Stream commands never offer
rendering options; unavailable commands are never offered. A dispatcher
forwards the completion of an external command to that command's own
`__complete`.

## Proof of implementation

Any command change updates, in the same change:

1. the catalog entry, declared with the `MAELYS_CLI_*` macros;
2. the handler consuming the validated invocation;
3. the JSON Schema file when the data shape changes (embedded with
   `maelys-cli-embed`, referenced with `MAELYS_CLI_SCHEMA`);
4. the tests of the catalog, options and envelopes;
5. the generated reference (`maelys-cli-reference`, checked by
   `contract-check`).

`maelys_cli_catalog_validate()` runs at every startup and in tests. It
checks identifier and pattern validity and uniqueness, summaries, value
declarations and defaults, `depends_on`/`depends_on_all`/`conflicts_with`
targets and groups, schema JSON validity, the synopsis length, the presence
of `--apply` on transactions and the handler-or-delegate-or-unavailable rule.
The conformance kit of the specification then proves, from the outside,
that the binaries speak `agent-cli/v2`.
