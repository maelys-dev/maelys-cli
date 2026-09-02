# Using Maelys CLIs from an agent

## Minimal sequence

```sh
PROGRAM describe --summary --format json --compact --non-interactive
PROGRAM describe note.write --format json --compact --non-interactive
PROGRAM note write /tmp/a.txt --content hello --format json --compact --non-interactive
# Review data.mode == "plan" and the preconditions.
PROGRAM note write /tmp/a.txt --content hello --apply --format json --compact --non-interactive
```

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
- never pass rendering options to a `protocol-stream` command;
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

## Dispatcher

`maelys COMMAND ...` starts the external program declared by a verified
manifest. `maelys commands list --format json` returns the accepted
commands; `maelys COMMAND describe --format json` returns that program's own
catalog. `maelys agents install DIR --apply` installs these instructions in
a project.
