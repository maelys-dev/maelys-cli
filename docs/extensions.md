# External commands

## Model

An extension is a separate executable, never a shared object. The `maelys`
dispatcher replaces itself with `execve(executable, ["executable", args...])`
after verification. No shell parses the arguments, no PATH is searched, no
plugin ABI is loaded.

## Manifest `maelys.cli-extension/v1`

```json
{
  "schema": "maelys.cli-extension/v1",
  "command": "oci",
  "executable": "/opt/homebrew/libexec/maelys/commands/maelys-oci",
  "cliApi": 1,
  "version": "0.1.0",
  "summary": "Manage verified OCI images and artifacts",
  "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
}
```

| Member | Required | Rule |
| --- | --- | --- |
| `schema` | yes | exactly `maelys.cli-extension/v1` |
| `command` | yes | `[a-z][a-z0-9-]*`, at most 63 characters, not `help`, `version` or `describe` |
| `executable` | yes | absolute path of a regular file, owned by root or the caller, not writable by group or world, owner-executable |
| `cliApi` | yes | unsigned integer equal to the dispatcher's `MAELYS_CLI_API` (1) |
| `version` | yes | free string reported by `commands list` |
| `summary` | no | one line shown by `help` |
| `sha256` | no | lowercase hex digest of the executable; when present it must match |

## Directories

Manifests named `*.json` are read, in lexical order, from:

```text
PREFIX/share/maelys/commands/          (compile-time PREFIX)
/opt/homebrew/share/maelys/commands/
/usr/local/share/maelys/commands/
/usr/share/maelys/commands/
```

`MAELYS_COMMANDS_PATH=/dir1:/dir2` replaces this list for development and
tests. Directories must be absolute; missing directories are skipped.

## Verification

The dispatcher refuses to start, with an `ACCESS_DENIED`, `PROTOCOL_FAILED`,
`UNSUPPORTED` or `VALIDATION_FAILED` diagnostic naming the file, when any
manifest is:

- not a regular file or reachable through a symbolic link;
- owned by another user than root or the caller;
- writable by group or world;
- larger than 64 KiB, not valid JSON or not an object;
- of another schema or another `cliApi`;
- pointing to an unusable executable or to a digest mismatch;
- declaring a command already declared by an earlier manifest.

A single invalid manifest blocks the whole dispatcher on purpose: a partial
catalog would let an agent believe a command is absent.

## Writing an extension

An extension should itself be built on `libmaelys_cli` so that
`maelys COMMAND describe --format json` returns the same descriptor shape.
It receives the arguments after the command word verbatim, including
`--format` and `--help`, and owns its stdout and exit code.

Install the binary under `PREFIX/libexec/maelys/commands/` and the manifest
under `PREFIX/share/maelys/commands/COMMAND.json` with mode `0644`. Packages
compute `sha256` at build time.

## Product-level delegates

A product CLI can also delegate one of its own commands to a helper without
the dispatcher, by declaring `.delegate = "helper-name"` (resolved beside the
executable, in `../libexec/PROGRAM`, `../libexec` and the application's
`helper_directories`) or an absolute path. The same executable checks apply.
This is how `maelys-warden image` can hand over to the OCI tools until it is
replaced by `maelys oci`.
