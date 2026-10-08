# Using Maelys CLIs from an agent

The contract is `agent-cli/v2`, specified in
[maelys-dev/agent-cli-spec](https://github.com/maelys-dev/agent-cli-spec):
discovery through `describe --summary`, descriptors with typed `input` and
`outputSchema`, one envelope on stdout for success and one on stderr for
failure, exit codes `0` (completed), `1` (failed) and `2` (a completed
validation report with violations), eleven stable error codes, plan by
default and `--apply` for transactions, `--dry-run` refused, protocol
streams reserving stdout. Read the specification for the rules; this page
lists only what a CLI built on `libmaelys_cli` adds.

## Minimal sequence

```sh
PROGRAM describe --summary --format json --compact --non-interactive
PROGRAM describe --summary --prefix repo --format json --compact --non-interactive   # one namespace
PROGRAM describe note.write --format json --compact --non-interactive
PROGRAM note write /tmp/a.txt --content hello --format json --compact --non-interactive
# Review data.mode == "plan" and the preconditions.
PROGRAM note write /tmp/a.txt --content hello --apply --format json --compact --non-interactive
```

When `describe` shows `--expect` among a transaction's options, bind the
application to the plan you reviewed: read `data.fingerprint` from the plan
and pass it back.

```sh
fingerprint=$(PROGRAM note write /tmp/a.txt --content hello --field fingerprint)
PROGRAM note write /tmp/a.txt --content hello --apply --expect "$fingerprint" --format json --compact --non-interactive
```

`PRECONDITION_FAILED` then means that the action or the state it touches
has changed since the plan and that nothing was written: plan again and
review. After an `--apply` whose outcome you cannot tell, plan again too:
the same fingerprint says the action is still to be done.

Verify `contract == "agent-cli/v2"` and `schemaVersion == 2`; identify a
command by `id`; never replay a `PRECONDITION_FAILED` blindly; prefer
`--compact` to save tokens.

## What libmaelys_cli adds on top of the specification

- The trunk options `--progress`, `--verbose` and `--pager` (spec 2.3) are
  in `globalOptions`; they write nothing under `--format json` or `jsonl`,
  so an agent's envelope stays alone on its stream, and a pager never
  starts in a pipe. `argument.pattern` is enforced (`VALIDATION_FAILED`),
  the summary form carries no catalog-wide member, and text records into a
  pipe are tab-separated rows.
- `--field NAME` (spec 2.4) renders one top-level member of `data` instead
  of the whole result; useful for an agent that wants a single value or
  list without parsing the full envelope. It refuses an explicit `--format
  json`/`--json` (`VALIDATION_FAILED`: a filtered envelope would not
  validate against `outputSchema`) and any name absent from `data`, so an
  agent that already asked for JSON should never add it; it is otherwise
  compatible with `jsonl`, which `--field` accepts on any command. On a
  command that can write (a transaction or an `execute`) only a name in the
  `required` of its `outputSchema` is accepted, and the refusal comes before
  the command runs (spec 2.9): read `required` in `describe` before asking.
- A hidden option (spec 2.2) is listed by `describe` with `hidden: true`,
  absent from `usage`, `help` and the completion, and accepted by the
  parser; `--trace` of `maelys-hello greet` is the example.
- `describe --summary --prefix PREFIX` (spec 2.1) returns one command
  namespace (`PREFIX` itself and `PREFIX.*`) with a `filter` member; an
  agent checks that the `describe` descriptor declares `--prefix` before
  using it.
- `describe COMMAND_ID` and `describe --summary` are minimal: no
  `globalOptions`, `output` or `invariants`; those come with `describe`
  alone, the complete catalog.
  Operands describe their value exactly as option arguments do — `type`,
  `choices`, limits, `digits`, `algorithms`, `pattern` (spec 2.6);
  `input.constraints` states the cross-option rules — `requires`,
  `at-most-one`, `exactly-one`, `all-or-none` — and an agent reads
  `exactly-one` there or nowhere, the options carrying no form of it (spec
  2.5); an entry's `options` is the whole rule; a descriptor with
  `available: false` names a command this build cannot run, do not invoke
  it.
- `MAELYS_CLI_FORMAT=json` in the environment selects JSON without
  options. It is the only way to obtain a JSON failure envelope from a
  `protocol-stream` command, whose stdout stays with the protocol and which
  refuses rendering options.
- `json-records` commands render three ways: `--format json` (`{"count",
  "records"}` in the envelope), `--format jsonl` (one compact object per
  line, no envelope; failure is still an envelope on stderr) and text (one
  human line per record).
- A descriptor may carry `examples`: real invocations of the command, each
  `{"words": [...], "summary": "..."}`, `words` being the command line
  without the program's name. They are in `describe` and in `describe
  COMMAND_ID`, not in the summary. The program parsed each one when it
  started, so an example is a line the command accepts as it stands.
- `PROGRAM help` is for a person: it names each command by its pattern and
  its purpose within 80 columns, and `PROGRAM help conventions` spells out
  the global options and the agent contract. `PROGRAM help COMMAND_ID` gives one
  command, `PROGRAM help FAMILY` (or `PROGRAM FAMILY --help`) the commands
  under an identifier with their usage. An agent reads `describe`, whose
  shape is a contract; the text of `help` is not one.
- `PROGRAM completion bash|zsh|fish` prints the shell completion generated
  from the catalog; candidates come from the hidden
  `PROGRAM __complete -- WORDS...` (a `json-records` command, usable with
  `--format json` before the `--`). Command identifiers are completed after
  `help` and `describe`; commands reported `available: false` are never
  offered.

## Dispatcher

`maelys COMMAND ...` starts the external program declared by a verified
manifest (`maelys.cli-extension/v1`), passing every argument verbatim.
`maelys commands list --format json` returns the accepted commands;
`maelys COMMAND describe --format json` returns that program's own catalog;
`maelys __complete -- COMMAND ...` forwards to that program's own
`__complete`. `maelys agents install DIR --apply` installs these
instructions in a project.
