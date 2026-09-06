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
catalog is composed at startup with `maelys_cli_catalog_concat()` from its
built-in commands plus one delegate entry per verified manifest. Products
with build variants use the same composition: a later part replaces a
descriptor the base catalog declares `.unavailable`.

## Control flow of `maelys_cli_main()`

1. `maelys_cli_catalog_validate()` refuses a malformed catalog before any
   argument is read. A product cannot ship a command that `describe`
   would misrepresent.
2. A pre-scan of `argv` detects `--format`, `--json`, `--compact` and
   `--color`, after `MAELYS_CLI_FORMAT` from the environment, so that even
   a parse failure is reported in the requested format; for stream commands
   the environment default only shapes the stderr failure envelope.
3. `maelys_cli_parse()` resolves the command by longest pattern match, then
   validates in causal order. Delegate commands stop after the pattern and
   collect every remaining argument verbatim.
4. `help`, `version`, `describe`, `completion` and the hidden `__complete`
   are built-in commands with the same descriptor shape as product
   commands; `--help` after a command renders that command's help, and
   `__complete` derives shell candidates from the catalog.
5. The handler runs with a `maelys_cli_context_t`. It must reply exactly
   once; the runtime turns a silent handler into an `UNEXPECTED` failure.
6. Delegates are resolved beside the executable, in `../libexec/PROGRAM`,
   in `../libexec` and in the application's `helper_directories`, then
   replaced with `execve`; scripts require an absolute shebang interpreter
   not named `env`, so this path performs no implicit interpreter lookup.

## Rendering

| format | success | records | failure |
| --- | --- | --- | --- |
| `text` | `human` string, or indented data when NULL | `human_line` per record | `program: [CODE] message` + `Hint:` on stderr |
| `json` | one envelope on stdout | envelope with `data.records` | one envelope on stderr |
| `jsonl` | not allowed for envelope commands | one compact object per line | one envelope on stderr |

Key order in envelopes is fixed: `schemaVersion`, `contract`, `command`,
`ok`, `exitCode`, then `data` or `error`. `--compact` selects one line;
otherwise two-space indentation.

## Who reads and writes JSON in the Maelys family

Decision: `libmaelys_cli` writes CLI output (envelopes, `describe`,
records) with its own order-preserving writer and syntax-checks what
handlers hand it; it reads nothing untrusted. `maelys-json` is the reader
and the canonical writer of the family: bounded parsing, duplicate-key and
UTF-8 rejection, canonical bytes for anything signed or hashed. The only
place where the framework reads untrusted JSON, extension manifests, lives
in `libmaelys_cli_extension.a`, which links maelys-json; a product that
only builds a CLI links the core and no JSON library at all. Handler data
built with any writer is handed to the framework as text.

## Why no JSON library in the core

The core needs two JSON operations: build small documents with a stable
key order, and validate and re-indent handler output. `src/json.c`
implements exactly those in about 400 lines. Anything beyond, in
particular reading documents that cross a trust boundary, is not
reimplemented: it is maelys-json, linked only where such reading happens.

## Why external processes instead of plugins

`dlopen()` plugins would share the address space, ABI and crash domain of
the dispatcher. External commands started with `execve` keep dependencies,
licenses and failure isolated, can be installed and removed as ordinary
files, and can be implemented in another language while still honoring the
`agent-cli/v2` contract. Discovery is restricted to manifests in fixed
directories so that nothing on PATH can silently become a `maelys` command.

## Python

`python/maelys_cli.py` implements the same contract for Python products,
without depending on the C library: one file, standard library only. The
catalog vocabulary mirrors the C declaration macros so that a product reads
the same in both languages; the conformance kit judges both from the
outside. `python/examples/hello.py` is its reference product.
