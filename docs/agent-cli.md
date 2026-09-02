# Using Maelys CLIs from an agent

## Minimal sequence

```sh
PROGRAM describe --summary --format json --compact --non-interactive
PROGRAM describe note.write --format json --compact --non-interactive
PROGRAM note write /tmp/a.txt --content hello --format json --compact --non-interactive
# Review data.mode == "plan" and the preconditions.
PROGRAM note write /tmp/a.txt --content hello --apply --format json --compact --non-interactive
```

`describe COMMAND_ID` returns one minimal descriptor (no `globalOptions`,
`output` or `invariants`; those come with `describe` and
`describe --summary`). Read `input.operands[].type` and `choices`,
`input.options[].argument`, `default`, `requires`, `group` and
`input.constraints` (`requires`, `at-most-one`, `all-or-none`). A descriptor
with `available: false` names a command this build cannot run; do not
invoke it.

Do not build a command from the `help` text. Use `data.commands[].input`,
then read only the stdout envelope. On exit `1`, stdout is empty: parse the
stderr envelope and follow `error.hint`. On exit `2`, the call itself
succeeded and `data` reports the violations.

## Principles

- identify a command by `id`, never by its human label;
- verify `contract == "agent-cli/v2"` and `schemaVersion == 2`;
- treat `data` as governed by the `outputSchema` of the installed descriptor;
- add `--apply` only after reviewing the plan of the same invocation;
- copy plan preconditions into dedicated options when they exist;
- never replay a `PRECONDITION_FAILED` blindly; read the state again;
- never pass rendering options to a `protocol-stream` command; set
  `MAELYS_CLI_FORMAT=json` in its environment to get a JSON failure
  envelope on stderr while its stdout stays with the protocol;
- never mix stdout and stderr;
- prefer `--compact` to save tokens; it does not change the schema.

## Records

A `json-records` command supports three renderings:

```sh
PROGRAM list --format json      # {"count": N, "records": [...]} in the envelope
PROGRAM list --format jsonl     # one compact object per line, no envelope
PROGRAM list                    # one human line per record
```

With `jsonl`, failure is still an envelope on stderr and the exit code.

## Shell completion

`PROGRAM completion bash|zsh|fish` prints a shim; the candidates come from
`PROGRAM __complete -- WORDS...` (a hidden `json-records` command, also
usable with `--format json` before the `--`). Command identifiers are
completed after `help` and `describe`; commands reported `available: false`
are never offered; the `maelys` dispatcher forwards the completion of an
external command to that command's own `__complete`.

## Dispatcher

`maelys COMMAND ...` starts the external program declared by a verified
manifest. `maelys commands list --format json` returns the accepted
commands; `maelys COMMAND describe --format json` returns that program's own
catalog. `maelys agents install DIR --apply` installs these instructions in
a project.
