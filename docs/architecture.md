# Architecture

## Layers

```text
┌──────────────────────────────────────────────────────────────────────┐
│ product CLI (maelys-warden, maelys-git, maelys-oci, ...)             │
│   catalog: maelys_cli_command_t[]      handlers: maelys_cli_handler_t │
├──────────────────────────────────────────────────────────────────────┤
│ libmaelys_cli                                                        │
│   app.h        maelys_cli_main, help, version, describe, rendering,  │
│                delegation, handler accessors                         │
│   invocation.h parser, error codes, exit codes                       │
│   catalog.h    descriptors, synopsis derivation                      │
│   json.h       writer, validator, formatter, member lookup           │
│   process.h    trusted execve without shell, helper resolution       │
│   extension.h  manifest discovery for dispatchers                    │
│   values.h environment.h files.h digest.h terminal.h                 │
├──────────────────────────────────────────────────────────────────────┤
│ POSIX (Linux, macOS)                                                 │
└──────────────────────────────────────────────────────────────────────┘
```

`maelys` (the dispatcher) is itself a product CLI of the framework whose
catalog is built at startup from built-in commands plus one delegate entry
per verified manifest.

## Control flow of `maelys_cli_main()`

1. `maelys_cli_catalog_validate()` refuses a malformed catalog before any
   argument is read. A product cannot ship a command that `describe`
   would misrepresent.
2. A pre-scan of `argv` detects `--format`, `--json`, `--compact` and
   `--color` so that even a parse failure is reported in the requested
   format.
3. `maelys_cli_parse()` resolves the command by longest pattern match, then
   validates in causal order. Delegate commands stop after the pattern and
   collect every remaining argument verbatim.
4. `help`, `version` and `describe` are built-in commands with the same
   descriptor shape as product commands; `--help` after a command renders
   that command's help.
5. The handler runs with a `maelys_cli_context_t`. It must reply exactly
   once; the runtime turns a silent handler into an `UNEXPECTED` failure.
6. Delegates are resolved beside the executable, in `../libexec/PROGRAM`,
   in `../libexec` and in the application's `helper_directories`, then
   replaced with `execve`.

## Rendering

| format | success | records | failure |
| --- | --- | --- | --- |
| `text` | `human` string, or indented data when NULL | `human_line` per record | `program: [CODE] message` + `Hint:` on stderr |
| `json` | one envelope on stdout | envelope with `data.records` | one envelope on stderr |
| `jsonl` | not allowed for envelope commands | one compact object per line | one envelope on stderr |

Key order in envelopes is fixed: `schemaVersion`, `contract`, `command`,
`ok`, `exitCode`, then `data` or `error`. `--compact` selects one line;
otherwise two-space indentation.

## Why no JSON library in the core

The framework needs three JSON operations: build small documents, validate
and re-indent handler output, and read flat manifests. `src/json.c`
implements exactly those in about 600 lines with strict syntax rules and a
depth limit. Products keep the freedom to use Jansson or any other model for
their own data; they hand the framework serialized text.

## Why external processes instead of plugins

`dlopen()` plugins would share the address space, ABI and crash domain of
the dispatcher. External commands started with `execve` keep dependencies,
licenses and failure isolated, can be installed and removed as ordinary
files, and can be implemented in another language while still honoring the
`agent-cli/v2` contract. Discovery is restricted to manifests in fixed
directories so that nothing on PATH can silently become a `maelys` command.
