# Maelys CLI

`maelys-cli` is the shared command-line framework of the Maelys tools. It
turns what was built by hand in Maelys Git, Hermes and Warden into one
reusable, dependency-free C11 library plus one dispatcher:

```text
maelys-cli/
├── libmaelys_cli.a   the framework: catalog, parser, renderer, mechanics
└── maelys            the dispatcher: `maelys oci ...`, `maelys agents ...`
```

Every Maelys CLI built on it has the same shape for humans and agents:

```sh
PROGRAM describe --summary --format json --compact --non-interactive
PROGRAM COMMAND ... --format json            # plan, or read
PROGRAM COMMAND ... --apply --format json    # reviewed transaction
```

with one `agent-cli/v2` envelope on stdout for success, one on stderr for
failure, stable error codes and exit codes `0`, `1` and `2`.

## Constitution

A mechanism enters `libmaelys_cli` only when:

1. at least two Maelys CLIs need it (Warden, Git, Hermes, OCI, ...);
2. it knows no product type: no policy, MIR, receipt, repository or
   editorial concept;
3. it adds no external dependency: JSON is written and scanned by the
   library itself, Jansson stays optional in products;
4. Linux and macOS expose the same observable behavior;
5. it ships with focused positive and negative tests.

## Architecture

```text
product catalog (maelys_cli_command_t[])
        │  operands + typed options + constraints + effect + output mode
        ▼
shared parser ────────────► validated invocation
        │                      (typed values, causal error order)
        ▼
product handler ──────────► maelys_cli_succeed / emit_record / fail
        │
        ▼
shared renderer ──────────► human text | json envelope | jsonl records
```

| Layer | Header | Content |
| --- | --- | --- |
| mechanics | `values.h`, `environment.h`, `files.h`, `digest.h` | `u32`/`u64`/`i64`, sizes `K/M/G/T`, durations, booleans, choices, hex; `NAME=VALUE` overlays; bounded reads, atomic writes with explicit `REPLACE`/`NO_REPLACE`; trust checks; SHA-256 |
| output | `json.h`, `terminal.h` | incremental JSON writer, strict validator, formatter, member lookup; tty and color detection (`--color`, `NO_COLOR`, `CLICOLOR_FORCE`) |
| processes | `process.h` | absolute-path trusted executables, `run` and `replace` via `execve` without shell, helper resolution |
| contract | `catalog.h`, `invocation.h` | command, operand and option descriptors; parser; error codes; exit codes |
| runtime | `app.h` | `maelys_cli_main()`, built-in `help`, `version`, `describe`; rendering; delegation; handler accessors |
| dispatcher | `extension.h` | discovery of external commands through verified manifests |

Every public function is documented in [docs/api-reference.md](docs/api-reference.md).
The complete conventions are in [docs/command-conventions.md](docs/command-conventions.md),
the agent-facing contract in [docs/agent-cli.md](docs/agent-cli.md), the
external command model in [docs/extensions.md](docs/extensions.md) and the
ABI policy in [docs/abi.md](docs/abi.md). The generated reference of the
bundled programs is [docs/cli-reference.md](docs/cli-reference.md).

## Writing a product CLI

Output schemas are ordinary JSON Schema files kept next to the sources and
embedded at build time by `maelys-cli-embed` (installed with the framework,
path exported as the `embed` pkg-config variable). The catalog uses
designated initializers behind small macros, so every declaration reads as
prose and unspecified fields stay zero:

```text
schemas/note-write.json  ──maelys-cli-embed──►  generated/schemas.c + .h
                                                  extern const char note_write_schema[];
```

```c
#include <maelys/cli.h>
#include "schemas.h" /* generated */

static const maelys_cli_operand_t note_operands[] = {
    {MAELYS_CLI_OPERAND("FILE", "Destination file.")},
};
static const maelys_cli_option_t note_options[] = {
    {MAELYS_CLI_STRING("content", "TEXT", "Text to store."), .required = 1},
    {MAELYS_CLI_SIZE("memory", "BYTES", "Ceiling; K/M/G accepted.", 1u, 0u),
     .default_text = "1G"},
    MAELYS_CLI_APPLY_OPTION,
};

static int note_write(maelys_cli_context_t *context) {
    const char *path = maelys_cli_operand(context, 0u);
    const char *content = maelys_cli_option(context, "content");
    int apply = maelys_cli_flag(context, "apply");
    if (apply && maelys_cli_write_file_atomic(path, content, strlen(content),
            0644, MAELYS_CLI_WRITE_NO_REPLACE) != 0)
        return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_IO_FAILED, errno, path);
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "mode", apply ? "apply" : "plan");
    (void)maelys_cli_json_key_boolean(&data, "changed", apply);
    (void)maelys_cli_json_end_object(&data);
    return maelys_cli_succeed_writer(context, &data,
        apply ? "Note written." : "Plan only; add --apply.", MAELYS_CLI_EXIT_OK);
}

static const maelys_cli_command_t commands[] = {
    {MAELYS_CLI_TRANSACTION("note.write", "note write", "Store a note in a file.",
     note_write),
     MAELYS_CLI_OPERANDS(note_operands), MAELYS_CLI_OPTIONS(note_options),
     MAELYS_CLI_SCHEMA(note_write_schema)},
    /* An external helper: everything after `image` is passed verbatim. */
    {MAELYS_CLI_EXTERNAL("image", "image", "Manage images through maelys-oci.",
     "maelys-oci")},
};

int main(int argc, char **argv) {
    static const maelys_cli_app_t app = {
        .program = "maelys-notes", .product = "Maelys Notes", .version = "0.1.0",
        .commands = commands, .command_count = MAELYS_CLI_COUNT(commands),
    };
    return maelys_cli_main(&app, argc, argv);
}
```

Makefile fragment for the schemas:

```make
SCHEMAS := $(wildcard schemas/*.json)
SYMBOLS := $(foreach s,$(SCHEMAS),$(subst -,_,$(basename $(notdir $(s))))_schema=$(s))
generated/schemas.c: $(SCHEMAS); maelys-cli-embed $(SYMBOLS) > $@
generated/schemas.h: $(SCHEMAS); maelys-cli-embed --header $(SYMBOLS) > $@
```

With a C23 compiler (`-std=c23`, clang 19+, gcc 15+) the generator is
optional: `static const char note_write_schema[] = {` `#embed "schemas/note-write.json"` `, 0};`
does the same thing in the source itself. The framework stays C11 so both
paths work.

Command macros: `MAELYS_CLI_READ`, `MAELYS_CLI_RECORDS`,
`MAELYS_CLI_TRANSACTION` (preview, then apply with `--apply`),
`MAELYS_CLI_COMMIT_TRANSACTION`, `MAELYS_CLI_EXECUTE`, `MAELYS_CLI_STREAM`,
`MAELYS_CLI_EXTERNAL`. Option macros: `MAELYS_CLI_FLAG`, `_STRING`, `_PATH`,
`_UNSIGNED`, `_INTEGER`, `_SIZE`, `_DURATION`, `_CHOICE`, `_HEX`; attributes
`.required`, `.repeatable`, `.requires`, `.conflicts_with`, `.default_text`
follow the macro. Operands: `MAELYS_CLI_OPERAND`, `_OPERAND_OPTIONAL`,
`_OPERAND_REST`.

`maelys_cli_main()` validates the catalog, parses `argv`, provides `help`,
`--help`, `version`, `--version`, `describe`, the global options `--format`,
`--json`, `--compact`, `--pretty=false`, `--non-interactive` and `--color`,
renders the envelope and runs or delegates the command. The complete example
is [examples/hello/main.c](examples/hello/main.c) (`maelys-hello`) with its
schemas in [examples/hello/schemas/](examples/hello/schemas/); it also drives
the end-to-end tests.

## The `maelys` dispatcher

`maelys` runs external commands declared by installed manifests. There is no
`dlopen`, no PATH search and no shell: a crash in an extension cannot corrupt
the dispatcher, extensions keep independent dependencies and licenses, and
they can be written in any language.

```text
maelys
  │ execve, never a shell
  ▼
maelys-oci
```

```json
{
  "schema": "maelys.cli-extension/v1",
  "command": "oci",
  "executable": "/opt/homebrew/libexec/maelys/commands/maelys-oci",
  "cliApi": 1,
  "version": "0.1.0",
  "summary": "Manage verified OCI images and artifacts",
  "sha256": "optional digest of the executable"
}
```

Manifests are read from `PREFIX/share/maelys/commands/`,
`/opt/homebrew/share/maelys/commands/`, `/usr/local/share/maelys/commands/`
and `/usr/share/maelys/commands/`. The dispatcher verifies that the manifest
is a regular non-symlink file with a trusted owner and safe modes, that the
executable is absolute, regular, trusted and executable, that `cliApi`
matches, that the optional digest matches and that no command is declared
twice. `maelys commands list` shows what was accepted.

## Agent instructions for consumer projects

Installing the framework also installs the instructions that Claude Code and
Codex need to use and extend a Maelys CLI correctly. Apply them to a project
with:

```sh
maelys agents install /path/to/project           # plan
maelys agents install /path/to/project --apply   # write
maelys agents status /path/to/project            # exit 2 when outdated
```

This manages a marked block in `AGENTS.md` (Codex) and `CLAUDE.md` (Claude
Code), the complete guide `docs/maelys-cli-guide.md` and the Claude skill
`.claude/skills/maelys-cli-command/SKILL.md`. Text outside the markers is
preserved; generated files are replaced. `--client claude|codex` restricts the
set. The same texts are installed under `PREFIX/share/maelys-cli/agents/`
and `make agents-install PROJECT=DIR` runs the command from a source tree.

## Building and testing

```sh
make                 # libmaelys_cli.a, maelys, maelys-hello, pkg-config file
make check           # unit tests, end-to-end CLI tests, C++ header gate
make asan-ubsan      # the same under AddressSanitizer and UBSan
make install-check   # install into a scratch prefix and build a consumer
make install PREFIX=/opt/homebrew   # lib, headers, maelys, maelys-cli-embed, agent texts
make generate-cli-reference
```

Requirements: a C11 compiler, POSIX `make`, `sh`, `od` and `awk`; `python3`
only for the reference generator; a C++17 compiler only for the header gate.

## Roadmap

1. Extract `maelys-warden-guest`.
2. Ship `maelys-cli` (this repository) with the dispatcher and the shared
   mechanics.
3. Extract `maelys-oci` as an external command of `maelys`.
4. Migrate Warden's `cli/common` to `libmaelys_cli` and its `run` command to
   the catalog; replace `maelys-warden image` by `maelys oci`.
5. Migrate Maelys Git to the shared catalog, parser and renderer, keeping the
   `agent-cli/v2` envelope unchanged.
6. Migrate the other Maelys products. See [docs/migration.md](docs/migration.md).

## License

MIT. See [LICENSE](LICENSE).
