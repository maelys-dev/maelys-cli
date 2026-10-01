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
| `executable` | yes | absolute path, canonicalized, of a regular file owned by root or the caller, not writable by group or world, owner-executable, in a trusted directory |
| `cliApi` | yes | unsigned integer equal to the dispatcher's `MAELYS_CLI_API` (1) |
| `version` | yes | one line without terminal control characters, reported by `commands list` |
| `summary` | no | one line without terminal control characters, shown by `help` |
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

A manifest in one of these directories may be a symbolic link. Package
managers install that way — Homebrew keeps the files of a package in its
cellar and links them into the prefix, so
`/opt/homebrew/share/maelys/commands/oci.json` is a link to
`/opt/homebrew/Cellar/maelys-oci/VERSION/share/maelys/commands/oci.json` —
and the dispatcher follows it. See "Trust and symbolic links" below for what
is required of the file it resolves to.

## Verification

The dispatcher refuses to start, with an `ACCESS_DENIED`, `PROTOCOL_FAILED`,
`UNSUPPORTED` or `VALIDATION_FAILED` diagnostic naming the file, when any
manifest is:

- not a regular file;
- owned by another user than root or the caller;
- writable by group or world;
- held in a directory owned or writable by an untrusted user, which for a
  link is the directory at the other end: the diagnostic then names the
  path installed and the file it resolves to, since the first looks
  irreproachable on its own;
- larger than 64 KiB, not valid JSON or not an object;
- of another schema or another `cliApi`;
- carrying line, ANSI or bidirectional controls in `version` or `summary`;
- pointing to an unusable executable or to a digest mismatch;
- declaring a command already declared by an earlier manifest.

Every one of these is judged on the file the path resolves to; the
diagnostic names the path that was discovered, which is the one an operator
installed, and `commands list` reports it as `manifest`.

A manifest that cannot be trusted or understood blocks the whole dispatcher
on purpose — an untrusted file, invalid JSON, an unknown schema or `cliApi`
member, an invalid command name, a command declared twice: none of those can
build a catalog anyone should rely on, and skipping them with a warning would
be too easy to miss.

A manifest that is sound but declares a command **this machine cannot run**
is different, and costs nothing beyond that command. Its executable is gone,
or untrusted, or of another `cliApi`, or does not match the declared digest:
the command is declared, `describe` reports `available: false` with the
reason, `commands list` carries `available`, `unavailableReason` and
`unavailableCode`, completion never offers it, and invoking it answers the
code that names the cause — `NOT_FOUND` for an executable that is gone,
`ACCESS_DENIED` for a refusal of trust or a digest that does not match,
`UNSUPPORTED` for a `cliApi` this dispatcher does not provide. The
dispatcher declares those commands with `.unavailable` and
`.unavailable_code` of `maelys/cli/catalog.h`, which is how any product
says the same thing. Every other
command, including every built-in of the dispatcher, keeps working.

Until 0.5.32 any of those stopped the dispatcher entirely, `maelys --version`
included: one extension installed by a package manager whose digest no longer
matched its binary took the whole tool down. The worry that rule answered — an
agent must not believe a command is absent when it is merely broken — is
answered better by declaring the command and saying why it cannot run, which
is what `available: false` exists for.

## Trust and symbolic links

The rule is: **a manifest is trusted exactly as the executable it declares.**
A symbolic link is followed, and the file it resolves to must be a regular
file owned by root or the caller, not writable by group or world, held in a
directory owned by root or the caller and not writable by group or world.
That last requirement is `MAELYS_CLI_FILE_TRUSTED_DIRECTORY` of
`maelys/cli/files.h`; the directory is judged by the descriptor the file was
reached through, and the file read must still be an entry of it.

Until this rule existed the loader passed `MAELYS_CLI_FILE_NO_SYMLINK` and
refused a linked manifest outright. What that refusal was protecting against
is a link rewritten by whoever controls the directory it sits in — point the
name at content of their choosing and the dispatcher executes what they
name. The refusal is not what defeats that, and never was: someone who can
write that directory can replace a regular manifest just as easily. What
defeats it is knowing who may write the directory the path resolves to, and
that question has an answer for a link and for a plain file alike. So the
requirement moved from the shape of the entry to the trust of its directory,
which is strictly more than was asked before — a manifest whose own modes
are safe but whose directory is group-writable used to be accepted and is
now refused — and the dispatcher stopped refusing the one installation
layout every package manager uses.

Two consequences are worth stating plainly.

A manifest installed by Homebrew is trusted because the prefix belongs to
the user who runs `brew`. `/opt/homebrew` and the cellar under it are owned
by that user, so `maelys` run by that same user accepts them, and `maelys`
run as root or as another account does not: root must not trust a tree its
owner may rewrite. This is not new to manifests — the executable check has
always worked this way — and `sudo maelys oci` is refused for the same
reason `sudo` would refuse to run a binary out of a user-writable prefix.

The rule judges the resolved parent directory, not every ancestor. On macOS
`/opt/homebrew/Cellar` is group-writable (`drwxrwxr-x`, group `admin`), so
walking the ancestry would refuse Homebrew entirely while adding little: a
directory a third party substitutes must itself be owned by root or the
caller and closed to group and world before anything inside it is read.
What remains is the residue the framework has always carried and
`SECURITY.md` names — the dispatcher is not a privilege boundary against
the invoking user, and a caller who opens their own directories to others
has decided who they trust.

A manifest that does not parse is reported with the position of the failing
value twice over: its RFC 6901 JSON Pointer for a machine (`Manifest /path
is not valid JSON at /version: ...`) and the line, column and offset for a
human. The pointer is omitted when the failure is at the document root,
where it would name nothing, and when a key on its path carries a terminal
control — the metadata check above never ran for a document that did not
parse, so the pointer is held to the same rule as `version` and `summary`.

## Linking a dispatcher

Manifest discovery is `libmaelys_cli_extension.a`, separate from the core so
that plain product CLIs stay dependency-free. A dispatcher links, in this
order, `libmaelys_cli_extension.a`, `libmaelys_cli.a` and one
`libmaelys-json.a` (pkg-config `maelys-cli-extension` declares the
`Requires`; CMake `maelys::cli_extension` links `maelys::json` publicly).
Manifests are parsed with a 64 KiB, depth 8, 1024-token budget; duplicate
members and invalid UTF-8 make a manifest invalid.

## Writing an extension

An extension should itself be built on `libmaelys_cli` so that
`maelys COMMAND describe --format json` returns the same descriptor shape.
It receives the arguments after the command word verbatim, including
`--format` and `--help`, and owns its stdout and exit code.

Install the binary under `PREFIX/libexec/maelys/commands/` and the manifest
under `PREFIX/share/maelys/commands/COMMAND.json` with mode `0644`. A
package manager that links the manifest there from its own store needs no
special handling, as long as the directory holding the real file is owned by
root or by the user who will run `maelys` and is closed to group and world.
Packages compute `sha256` over the binary as it will be installed, not as
it was built. A packager that rewrites a path inside a Mach-O binary signs
it again afterwards — Homebrew does, which is visible as `flags=0x2(adhoc)`
and a hash-suffixed identifier under `codesign -dv`, where a binary it left
alone still carries the linker's own `flags=0x20002(adhoc,linker-signed)` —
and those bytes are not the bytes the build hashed. A digest taken too early
makes the dispatcher refuse the extension with `ACCESS_DENIED`, and refuse
the whole catalog with it. Compute the digest in the step that installs the
manifest, after any relocation, or declare no `sha256`: the member is
optional and the executable's ownership and modes are checked either way. A script uses a direct absolute interpreter
in its shebang; relative interpreters and `#!/usr/bin/env ...` are refused
because they perform implicit current-directory or `PATH` lookup.

## Product-level delegates

A product CLI can also delegate one of its own commands to a helper without
the dispatcher, by declaring `.delegate = "helper-name"` (resolved beside the
executable, in `../libexec/PROGRAM`, `../libexec` and the application's
`helper_directories`) or an absolute path. The same executable checks apply.
This is how `maelys-warden image` can hand over to the OCI tools until it is
replaced by `maelys oci`.
