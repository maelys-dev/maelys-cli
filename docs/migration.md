# Migrating existing Maelys CLIs

## Warden (`cli/common` and `cli/maelys-warden.c`)

`cli/common.h` maps one-to-one onto `maelys/cli/values.h`,
`maelys/cli/environment.h` and `maelys/cli/files.h`:

| Warden | maelys-cli |
| --- | --- |
| `maelys_cli_parse_u64_decimal` | same name and contract |
| `maelys_cli_parse_u32_decimal` | same |
| `maelys_cli_parse_byte_size` | same, plus `T` suffix |
| `maelys_cli_string_list_*` | same |
| `maelys_cli_environment_*` | same, plus `get` and `to_envp` |
| `maelys_cli_read_regular_file` | same |
| `maelys_cli_write_file_atomic` | same; `NO_REPLACE` also refuses an existing symlink |

The hand-written `run` parser becomes one catalog entry. Its option table
already lists every option with a kind: `--vm-cpus` is `UNSIGNED`,
`--vm-memory` is `SIZE`, `--wall-time` becomes `DURATION` (`600s`) or
stays `UNSIGNED` milliseconds during the transition, `--network-frontend`
is a `CHOICE` of `auto|fd4|proxy`, `--env` is a repeatable `STRING`,
`--allow-read` a repeatable `PATH`. Mutual exclusion of `--ro`, `--rw` and
`--rootfs-only` is expressed with `conflicts_with`; the remaining semantic
checks (OCI requires `v9`, resource budgets require `--image`) stay in the
handler with `PRECONDITION_FAILED` or `VALIDATION_FAILED`. `run` is a
`stream` command: the workload owns stdout and the exit code.

`maelys-warden image ...` becomes a delegate entry
(`.delegate = "maelys-warden-oci-materializer"` plus a dedicated `pull`
entry) until `maelys oci` replaces it. Warden's search order (`beside`,
`../libexec`, Homebrew and system libexec) is expressed through
`helper_directories`; the `MAELYS_OCI_MATERIALIZER` override becomes an
absolute delegate chosen by the product before calling `maelys_cli_main()`.

## Maelys Git (`src/cli/catalog.c` and `src/cli/cli.c`)

The descriptor types are the same concept with more kinds. `OID` becomes
`MAELYS_CLI_HEX_OR(name, "OID", summary, 40u, 64u)` and `SHA256` becomes
`MAELYS_CLI_HEX(name, "SHA256", summary, 64u)`. The per-protocol output
modes (`git-smart-protocol-stream`, `git-hook-stream`,
`maelys-git-agent-jsonl-stream`, `maelys-git-events-jsonl-stream`) become
`MAELYS_CLI_PROTOCOL_STREAM(..., "git-smart")` and so on: `describe` reports
`outputMode: "protocol-stream"` plus `protocol`. `program` disappears: each
binary declares its own `maelys_cli_app_t` with its subset of commands and
`tools/generate_cli_reference.py BIN...` aggregates them. `synopsis` may
stay as an explicit override during the migration and then be dropped in
favor of the derived form. `output_schema_json` keeps the same strings, or
moves to JSON Schema files embedded with `maelys-cli-embed`.

`maelys_cli_emit_success(invocation, json_t *data, human, exit_code)`
becomes `maelys_cli_succeed(context, json_dumps(data), human, exit_code)`:
the product keeps Jansson for its data and hands serialized text to the
framework. `maelys_cli_emit_maelys_error()` becomes a product helper mapping
`maelys_git_result_t` to `maelys_cli_fail()` codes; the table in
`docs/command-conventions.md` is unchanged.

Hook limits that exist as library constants are declared once with
`MAELYS_CLI_DEFAULT_OF(MAELYS_GIT_HOOK_...)`; the French reference is
generated with `maelys-cli-reference --title --intro-file --columns
--global-label`.

Mirror and pull-request commands whose preconditions go together use
`.group = "preconditions"` on the precondition options and
`.depends_on_all` on `--apply`; hook limits declare their defaults once in
`default_text`; the Cloud-less build declares `run` with `.unavailable`.
The CMake build consumes the framework through `find_package(maelys-cli)`
or FetchContent pinned to a tag, and installs `maelys-cli-reference`
instead of copying the generator.

The envelope, `describe` shape, error codes and exit codes are preserved, so
`tools/generate_cli_reference.py` and the agent documentation keep working.
The only visible additions are `external`, `hidden`, `passthrough`,
`framework` and `cliApi` in `describe`, and richer `argument` metadata.

## Every extension publisher, at the trusted-manifest rule

This concerns the repositories that install a manifest under
`PREFIX/share/maelys/commands/` — maelys-oci and maelys-egress today — and
asks them for nothing, which is the point of recording it.

The dispatcher used to pass `MAELYS_CLI_FILE_NO_SYMLINK` when reading a
manifest and so refused one that was a symbolic link. Homebrew links
everything it installs from its cellar into the prefix, so every
brew-installed manifest was refused and `maelys` would not start at all on a
machine that had one: `[ACCESS_DENIED] Manifest
/opt/homebrew/share/maelys/commands/egress.json is untrusted: path is a
symbolic link`. The requirement is now
`MAELYS_CLI_FILE_TRUSTED_DIRECTORY`: the link is followed, and the file it
resolves to must be a regular file owned by root or the caller, not writable
by group or world, in a directory owned by root or the caller and not
writable by group or world. `docs/extensions.md` states the rule and what it
protects.

A formula needs no change. Two things are worth checking on the publishing
side, because the dispatcher refuses the whole catalog rather than one
command when either is wrong:

- the directory holding the real manifest inside the package's own store is
  closed to group and world (`0755`, as Homebrew creates it);
- the `sha256` of the executable is computed over the binary as installed,
  not as built. Homebrew rewrites paths inside a Mach-O binary and signs it
  again afterwards, which changes its bytes; `codesign -dv` shows
  `flags=0x2(adhoc)` with a hash-suffixed identifier on a binary it touched,
  against the linker's own `flags=0x20002(adhoc,linker-signed)` on one it did
  not. A digest taken before that step makes `maelys` refuse the extension,
  and the whole catalog with it. This is observable today: of the two
  extensions installed from the tap, `maelys-egress` 0.20.0 matches its
  digest and `maelys-oci` 0.6.6 does not, because only the latter was
  relocated. Compute the digest after relocation, or declare no `sha256` —
  the member is optional and the executable's ownership and modes are checked
  either way.

## Every consumer, at agent-cli-spec 2.3.0 (maelys-cli 0.5.18)

- `.pattern` is now enforced by the parser: a product that declared a
  pattern as documentation must make sure its values match, and rewrite
  any `(?:` group, `\d` class or lookaround into the common subset of
  ECMA-262 and POSIX ERE.
- Text records into a pipe are tab-separated rows; a test that compared the
  `human_line` of `maelys_cli_emit_record()` in a pipe compares the rows
  now (the human line is shown on a terminal only). `jsonl` is unchanged.
- `--progress`, `--verbose` and `--pager` exist on every command; a product
  option with one of these spellings must have the trunk's shape and
  meaning, or be renamed.

## A transaction that binds its application to the reviewed plan

- Optional, and nothing changes for a transaction that declares none of it.
  A transaction whose plan has a stable identity declares
  `MAELYS_CLI_EXPECT_OPTION` (C) or `expect=True` (Python), lists
  `fingerprint` in the `required` of its output schema, builds the
  fingerprint with `maelys_cli_fingerprint_*()` or `cli.Fingerprint`, calls
  `maelys_cli_expect()` or `invocation.expect()` before its first write and
  returns `data.fingerprint` in the plan and in the application.
- A product that already bound a plan under another spelling -- a revision
  it compared itself, a precondition object -- moves to this one: on a
  transaction `--expect` has this meaning and no other, and the catalog
  validation refuses another shape of it.
- A product that had an `--expect` of another meaning on a transaction
  renames it; on a read it is untouched.
## Every consumer whose program declares `agent_guidance`

- The guidance is wrapped to the width of the help, each of its lines being
  a paragraph kept at its own indentation. A paragraph written on one line
  needs nothing. One broken by hand into lines of 80 columns is joined back
  into one line per paragraph, or each hand-made line is wrapped on its own
  and the paragraph reads ragged at another width.

## Every consumer, at agent-cli-spec 2.21.0

2.21.0 writes, in its section 5 ("Language"), the language of the human
text, and this pin implements it; `docs/command-conventions.md`,
"Languages of human text", says how. What a product meets:

- **`--lang TAG` is a global option of every command**, in `globalOptions`
  and in the completion; a product that declares an option `--lang` of its
  own is refused at startup, as for any global option. A delegate receives
  it verbatim.
- **`version` carries `lang`**, the canonical language: `en`, or what
  `.canonical_language` (Python `canonical_language=`) names. A product
  whose catalog is not written in English declares it, or its `lang` is
  wrong: cx, written in French, sets `.canonical_language = "fr"`.
- **The framework's texts follow the language in force.** A product that
  declares nothing is unchanged in English. One that declares
  `canonical_language = "fr"` shows French headings, sentences and
  framework messages at once, under every locale, `C` included, until a
  line asks for a language it translates to.
- **`data.text` of `help` is the human text**, in the language of the
  invocation; describe, envelopes, `version`, the `commands` of `help`
  and `__complete` never change.
- **`maelys_cli_app_t` and `maelys_cli_invocation_t` gain members at their
  end** (`languages`, `language_count`, `canonical_language`; `lang`), and
  `maelys_cli_error_t` the template of its message. Designated initializers
  need nothing. A positional initializer of `maelys_cli_app_t` built with
  `-Wmissing-field-initializers -Werror` adds `NULL, 0u, NULL` at its end.
- To translate: write failures as templates (`maelys_cli_fail(context,
  code, hint, "No machine %s.", name)`, Python `cli.failure`), never
  assembled before, and declare one table per language; `maelys-cli-man
  --lang TAG --untranslated PROGRAM` lists what a table lacks.

## Every consumer, at agent-cli-spec 2.20.0

The pin comes with the pattern the library reads itself, the section
below. 2.20.0 writes, in its section 3, the subset an `argument.pattern`
is written in and how it reads; this release reads it so in C and in
Python, and its kit holds a product to it:

- the kit checks that every pattern of a value, options, operands and
  global options, is in the subset: a product whose catalog declares one
  outside it fails there, as it fails at startup with this release;
- the kit sends a value with a final newline to a pattern that ends in
  `$` and expects `VALIDATION_FAILED`;
- a pattern beyond the budget (32 nested groups, 65 536 atoms expanded) is
  a check the kit skips, and a startup this release refuses.

## Every consumer, at the pattern the library reads itself

`argument.pattern` is no longer compiled with the libc's `regcomp()`: the
library reads the subset agent-cli/v2 writes a pattern in (section 3, as
agent-cli-spec 2.20.0 states it) and matches it itself, in C as in Python.
Nothing changes for a pattern written in that subset, which every product
known to this repository declares. What to search for:

- **A pattern is printable ASCII.** A literal `é`, or any character above
  U+007E, is refused at startup: a libc in the C locale read it byte by
  byte, and `é+` repeated its last byte only. No pattern of a product known
  here has one.
- **`.` reads as in ECMA-262**: any character but U+000A, U+000D, U+2028
  and U+2029. The libc's `.` matched all four; Python's `re`, all but
  U+000A. A value with a carriage return no longer matches `^a.c$`; a
  pattern that must accept one says so with a class.

- **A pattern the subset leaves out is refused at startup**, `UNEXPECTED`
  with the reason, where the libc accepted it and read it its own way:
  `(?:...)` (tolerated and rewritten until now), `\d`, `\w`, `\s` (a
  digit for Python, a `d` for the TRE of macOS), `[[:alpha:]]`, `\` or
  `]` inside a bracket expression, a `-` after a range (`[a-z-9]`, a range
  for RE2, a character for ECMA-262), a bound with a leading zero
  (`{0,054}`), a lazy `*?`, an empty alternative, a bare `]`, `{` or `}`.
  Grep the `.pattern` and `pattern=` of the catalog for
  them; each has a spelling inside the subset (`(` for `(?:`, `[0-9]` for
  `\d`, `[A-Za-z]` for `[[:alpha:]]`, `\]` for `]`).
- **A bound above 255 is refused**: `{0,1000}` was already refused by the
  macOS libc (`RE_DUP_MAX`) and accepted by glibc, so a product that
  declared one ran on one platform only. Bound the length in the product's
  handler, or write `*` and check the length there.
- **A test that read the diagnostic** of a refused pattern reads "declares
  a pattern outside the common subset of ECMA-262 and POSIX ERE: REASON"
  instead of "is not a valid extended regular expression".
- **A bounded repetition costs its text**: `^[A-Za-z0-9_][A-Za-z0-9_.-]{0,254}$`
  cost 5.6 MiB of peak memory and a millisecond at every start of cx on
  macOS, `--version` included; it costs a few hundred bytes now, and a
  product that had shortened a bound for that reason may write what it
  means.

## Every consumer, at agent-cli-spec 2.19.0

- Nothing a product does stops working: 2.19.0 adds one SHOULD to section
  8. Keep a listed code when the boundary is among them, and put the
  product's own cause in `error.issues` or in an `x-` member of `error`; a
  code of the product's own stays for a boundary the list does not name.
- **A product that answered a code of its own for a listed boundary**
  moves the code to the listed one and the cause into an issue, with
  `maelys_cli_fail_error_extended()` (C, from this release) or
  `cli.Failure(..., issues=[...])` (Python). cx: `VALIDATION_FAILED` with
  the issue `PANES_CONFIG` at `panes.conf:12`, `NOT_FOUND` with
  `PANES_MACHINE_UNKNOWN` for an unknown machine.
- The invocation generator of 2.19.0 holds a program to the eleven codes
  only under `--reference`; elsewhere a code must be well formed.

## Every consumer, at agent-cli-spec 2.18.0

This pin comes with the release that lays the help out for a person; the
section below says what that changes. Two minors of the contract arrive
with it:

- **2.17.0** admits `help FAMILY` and nothing else that is not a command:
  `help conventions`, or any topic of a program's own, is `INVALID_COMMAND`.
  `help COMMAND_ID` must show the command's examples, the general help names
  no hidden command, and completion offers only identifiers after `help`
  and `describe`. A C or Python product on this release has all of it.
- **2.18.0** writes the order of colors: `--color always|never`, then
  `CLICOLOR_FORCE`, then `NO_COLOR`, then `TERM=dumb`, then whether the
  stream is a terminal; nothing in JSON is ever colored. It is the order
  this framework already followed.
- **Pin 2.18.0 with or after the release that carries this section**: the
  kit of 2.17.0 refuses `help conventions`, which an earlier framework
  answered.

## Every consumer, at the human help

- **A test that reads the text of `help`** changes: `EFFECT`, `OUTPUT`,
  `AGENT CONTRACT` and the full usage of a command are no longer in it.
  `describe --format json` has all of them, and its shape is the contract;
  test that instead. The usage of the help ends in `[options]` where
  `describe` lists each optional option.
- **`help conventions`** is refused with `INVALID_COMMAND`. A script or a
  document that sent a reader there points to `PROGRAM --help`, which now
  lists the global options whole.

## Every consumer, at agent-cli-spec 2.16.0

A program may now name a default command; one that names none is read as
before, and nothing here is required of it.

- **`default_command` is the last field of `maelys_cli_app_t`.** A product
  that initializes the structure by position and builds with `-Wextra
  -Werror` stops at `-Wmissing-field-initializers`: name the fields
  (`.program = ...`), or add a trailing `NULL`.
- **Declaring a default command can stop a line that ran.** The first word
  of the line then decides alone: `PROGRAM --json list` selects the default
  command and is refused in its name; `PROGRAM list --json` runs `list`. A
  Python product read the first form as `list` until then; a C product
  refused it already. Search the product's documents, tests and scripts for
  options written before the command name.
- **Its operands go after `--`**, and `-h` fails in its name: a line that
  names no command is the default command's in every respect.
- **A default command needs the release that carries this section**: an
  earlier framework has no field to declare one.

## Every consumer, at agent-cli-spec 2.15.0

Unlike the pins before it, this one comes with a framework release that
changes what a C product accepts. Three lines that worked are refused.

- **`-h` is not `--help`.** There is no short option. As the first word it
  names no command (`INVALID_COMMAND`); after a command it is refused like
  any word that starts with one dash. Search the product's documents, tests
  and scripts for ` -h`, and write `--help` or `help`.
- **A word that starts with one dash is refused before `--`**
  (`VALIDATION_FAILED`, in the name of the command). It was an operand in
  C. An operand that starts with a dash goes after `--`: `greet -- -5`.
  `-` alone is still an operand.
- **A command that takes another program's line takes it after `--`** as
  soon as one of its words starts with a dash: `run /bin/sh -c 'exit 3'`
  is refused, `run -- /bin/sh -c 'exit 3'` runs. A product with such a
  command (a stream command with a variadic operand) changes its tests, its
  examples and the synopsis it documents. A delegate is not concerned.
- **A size suffix is in capitals.** `512m`, `4k`, `2g`, `1t` are refused;
  `512M` is read. This holds for a `MAELYS_CLI_SIZE` option and for a
  direct call to `maelys_cli_parse_byte_size()`, whose reference said
  "either case". Search for a size value written in small letters.
- **A catalog is refused at startup** for an identifier outside
  `^[a-z][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*)*$` (an underscore, a leading
  digit, a final dot, two dots), for the identifier `unknown`, and for
  `preview`, `apply` or `commit` declared outside a transaction. A product
  sees it on its first run and in its tests, not in production.
- Nothing to do: of two options that set the same thing the last one
  written wins (`--json` and `--format`, `--compact` and `--pretty`), which
  both implementations already did; a Python product's values are now read
  exactly as C reads them (ASCII digits, 64 bits, a digest of its
  algorithm's length).
- **Pin 2.15.0 with or after the release that carries this section**, not
  before: its kit fails a C product built on an earlier framework on `help
  -x`, which that framework read as an operand.

## Every consumer, at agent-cli-spec 2.14.0

- **Pin 2.14.0 with or after the release that carries this section**, not
  before: its kit fails a product built on 0.6.5 or earlier on `COMMAND
  --help --format jsonl`, which exited 0 on an empty stdout, and a Python
  product on `describe --prefix NAME --help`, which was refused.
- 2.14.0 writes the order of the refusals as a numbered list (its section
  8), with `--help`, `--version` and an unavailable command in it. This
  framework follows it to the letter, in C and in Python; the list is in
  `docs/command-conventions.md`, "What the parser enforces".
- What a product may see change when it takes the framework release:
  - `COMMAND --help --format jsonl` fails with `VALIDATION_FAILED` in the
    name of `help`; add `--field NAME` or ask for `json`.
  - An unavailable command gives its help, refuses a wrong line as a wrong
    line, and answers its unavailable code to a rendering flag.
  - Python: `--help` on a line whose option lacks the one it requires gives
    the help; an option that ends the line without its value names the
    command in the failure envelope.
  A test that pinned one of the former answers pins the new one.
- The specification ships the test that found all of this:
  `python3 tests/invocations.py --apply --format-variable MAELYS_CLI_FORMAT
  PROGRAM` builds command lines from the program's catalog and holds the
  program to the contract on each. It runs every command that is not a
  stream, a delegate or `passthrough`, in a directory and a home made for
  the line: read what `--apply` means there before using it on a product
  whose plans reach a network.
- Left open by the contract, on purpose: what `--json=false` means when
  `MAELYS_CLI_FORMAT=json`. Here it selects text.

## Every consumer, at agent-cli-spec 2.13.1

- **Nothing to change.** 2.13.0 writes what `data.commands` of `help` holds:
  the identifiers of the commands the text shows, so the one asked for
  `help COMMAND_ID` and for `--help` after a command. Both implementations
  answered that before the specification wrote it, and its kit now checks
  it on `help version` and `version --help`. A product pins 2.13.1 with any
  release from 0.6.0 on.
- `help FAMILY` and a topic such as `help conventions` are this
  framework's, not the contract's: the commands of the family and an empty
  list are consistent with the sentence, and the kit does not check them.
- A hidden command is never offered and is answered when named. The general
  help lists none, which is what the kit checks; `help COMMAND_ID` and
  `--help` asked of a hidden command answer its help and name it in
  `data.commands`, as `describe COMMAND_ID` answers for it.
- Pin 2.13.1 rather than 2.13.0. The two ask the same and check the same,
  but 2.13.0 ended its sentence with "A hidden command is never among
  them", which read to the letter forbade the answer above. 2.13.1 replaces
  it at this repository's report; a program that passes one passes the
  other.

## Every consumer, at agent-cli-spec 2.12.0

- **Examples are declared in the catalog**, optionally:
  `MAELYS_CLI_EXAMPLES` of `MAELYS_CLI_EXAMPLE(words, summary)` in C,
  `examples=[cli.example(words, summary)]` in Python. A product that keeps
  invocation lines in a README moves the ones worth keeping: the catalog
  validation parses each at startup and refuses the catalog when one no
  longer parses, which a README never does. A product that declares none
  changes nothing.
- An example carries real values and no hidden option, and a word of it
  cannot hold a space. One that needs a 64-digit digest writes one.
- `--help` after a command gives its help and runs nothing, `--apply`
  included, and under `--format json` the envelope says `"command":
  "help"`. Both were true of this framework before the specification wrote
  them, the second since the release that carries this section; the 2.12.0
  kit checks them, so **a product pins 2.12.0 with or after that release**,
  not before: a C product built on 0.5.36 or earlier fails the kit on the
  envelope.
- 2.11.0 is skipped: it put the examples in the summary too.

## Every consumer, at the help that fits eighty columns

- The text of `help` changes, in all its forms; `describe` does not. A
  product test that compared a line of `help` compares the new one, or
  better, reads `describe`.
- `PROGRAM help` no longer spells out the global options nor the whole
  agent contract: it names the options and gives the first rule, and
  `PROGRAM help conventions` has both whole. A product test that looked for
  an option's description or for an exit code in `help` looks there.
- `PROGRAM help` no longer carries the usage of every command: it names
  each by its pattern and its purpose. The usage is in `help COMMAND_ID` and
  in the new `help FAMILY` / `FAMILY --help`, which lists the commands under
  an identifier. Nothing to declare; identifiers that share a prefix are
  what makes a family.
- `COMMAND --help --format json` of a C product answers in the envelope of
  `help`: `"command": "help"`, the command asked about in `data.commands`.
  It named the command itself, over data that command's output schema does
  not describe; a Python product already answered `help`. A consumer that
  matched the envelope's `command` against the command it asked about reads
  `data.commands` instead.
- A Python product's `help COMMAND_ID` now has the sections of a C
  product's: `USAGE`, the purpose, `EFFECT`, `OUTPUT`, `OPERANDS`,
  `OPTIONS`. It began with the usage line alone.
- The summary of the `help` operand changed, so a committed reference
  generated from `describe` changes by that sentence at the next adoption.

## Every consumer, at agent-cli-spec 2.10.0

- Nothing to change. A delegate declares the effect `execute` and no
  `protocol`, which `MAELYS_CLI_EXTERNAL` and `cli.external()` have always
  done; a product cannot declare one otherwise through the framework.
- The specification now says when to use which: `MAELYS_CLI_EXTERNAL` for a
  command whose words after the pattern are another executable's, handed
  over verbatim, its `--help` and its completion included;
  `MAELYS_CLI_STREAM` for a command that keeps its own options and operands
  and relays the stdio of a child it starts itself.

## Every consumer, at agent-cli-spec 2.9.0

- **`--field` on a command that can write is checked against the catalog,
  before the command runs.** A transaction, with or without `--apply`, and
  an `execute` accept only a name listed in the top-level `required` of
  their output schema. Until 0.5.35 the command ran, and a name its data
  did not carry was refused afterwards: a caller read `VALIDATION_FAILED`
  on a transaction that had been applied.
- What it asks of a product: list in `required` of `MAELYS_CLI_SCHEMA` (C)
  or `schema=` (Python) the members a writing command always returns. A
  writing command declared without a schema, or whose schema requires
  nothing, **no longer accepts any `--field`**; a member its schema leaves
  optional is refused even when the run would have carried it. A `read` is
  unchanged.
- `--expect FINGERPRINT` and a required `fingerprint` are reserved by 2.9.0
  for binding a plan to its application. Nothing in the framework offers
  them yet; a product must not declare an `--expect` of another meaning on
  a transaction.

## Every consumer, when a failure leaves stdout empty in `jsonl` too

- A `json-records` command writes its `jsonl` lines once it has succeeded,
  no longer as each record is emitted: a command that fails after emitting
  some leaves stdout empty, as it always did in `json` and in text. A
  consumer that read the lines of a long listing while it ran reads them at
  its end; a command whose output must flow while the work goes on is a
  protocol stream. Nothing to change in a handler.
- `MAELYS_CLI_FORMAT` is the default format, in C as it already was in
  Python: `--format` and `--json` override it, `--compact` and `--pretty`
  leave it in force. A C product run with `MAELYS_CLI_FORMAT=json` and
  `--compact` answers compact JSON where it answered text.
- `--field` against a json format the environment selected is refused before
  the command runs, as an explicit `--format json` already was. A caller
  that relied on a transaction being applied and then refused relied on a
  defect.

## Python products, at the static completion

- `PROGRAM completion SHELL` prints a script that carries the candidates of
  the catalog instead of calling `__complete` at every Tab. Nothing to
  declare; a product that pinned the text of the script in a test compares
  the new one, or `program.completion_script(shell, static=False)`.
- A script installed from a file is now a copy of the catalog as well as of
  the text: it is regenerated when the program is upgraded, by the package
  or by hand. `source <(PROGRAM completion bash)` needs nothing.
- A product whose catalog depends on the machine it runs on declares
  `static_completion=False`.
- The product pins agent-cli-spec 2.8.1 or later: the kit of 2.8.0 fails on
  macOS for a script of more than 4096 bytes whose line break falls on that
  boundary, which a script carrying its candidates can be.
- `shlex`, `subprocess` and `tempfile` are no longer imported with the
  module: a product that reached them as `maelys_cli.subprocess` imports
  them itself.

## Every consumer, at agent-cli-spec 2.8.0

- Nothing to change in a catalog or in a product built on 0.5.34 or later:
  2.8.0 writes down what that release already does. A hidden or unavailable
  command is offered neither as a word nor as an identifier after `help`
  and `describe`, and the kit now checks the identifiers; after a delegate's
  pattern the program adds no word of its own to the delegate's.
- The kit drives every bash it finds, `/bin/bash` included, under a check
  named after it: the bash 3.2 defect of 0.5.33 and earlier is reported on
  macOS even where a newer bash on the PATH hid it from the 2.7.0 kit.
- The kit takes a program named by a relative path again; the absolute
  paths 2.7.0 needed are no longer.

## Every consumer, at agent-cli-spec 2.7.0

- Nothing to change in a catalog. The completion scripts the framework
  prints were wrong in ways every product inherited: under bash 3.2, which
  is `/bin/bash` on macOS, none completed anything; under zsh the script of
  a C product failed on its first line of work, and the script of a Python
  product never fell back to files. A product receives the corrections by
  moving its pin; **a script already installed is a copy of the old text**
  and is regenerated — by the package at its next build, or by whoever
  wrote `PROGRAM completion SHELL` into a file by hand.
- `__complete` after an operand declared without a kind returns nothing
  where a C product returned `true` and `false`; a Python product no longer
  offers options on an empty word, only after `--`, as a C product always
  did. A test that compared those words compares the new ones.
- `__complete` after a delegate's pattern returns the delegate's words in
  JSON as in text, and no longer writes the delegate's absence on stderr.
- A dependency on the kit: 2.7.0 drives the scripts in bash, zsh and fish
  where they are installed, from a directory of its own, so it is given the
  program by an absolute path. A product that wants the three shells judged
  in CI declares `zsh` and `fish` under `[linux]` of `dependencies/packages`.

## Every consumer, at agent-cli-spec 2.6.0

- A hex or digest operand is now described conformantly: `digits` and
  `algorithms` on an operand, which the framework already emitted and the
  2.5.x schema refused, are what the contract says since 2.6.0. A product
  that avoided a typed operand for that reason may declare it.
- An operand takes `.pattern` as an option does, on a string or path kind,
  compiled at startup and enforced by the parser. A product that checked an
  operand's shape in its handler moves the pattern into the catalog and
  deletes the check.

## Every consumer, at agent-cli-spec 2.5.0

- An all-or-none entry of `input.constraints` no longer carries a `group`
  name; a test that compared the entry compares
  `{"kind":"all-or-none","options":[...]}` now. The name stays on each
  option's `group`, and the kit checks that a command's entries and its
  groups agree, so a `group` without its entry — which cannot happen with
  this framework, the entry being derived — fails the 2.5.0 kit.
- A rule the option fields cannot say is declared on the command:
  `MAELYS_CLI_CONSTRAINTS(array)` of `MAELYS_CLI_CONSTRAINT(kind, options)`
  — `exactly-one` above all, which has no option-level form. A product that
  enforced such a rule in its handler (two of five options given, none
  given) moves the rule into the catalog and deletes the check: `describe`
  states it and the parser refuses it before the handler runs.

## Every consumer, at agent-cli-spec 2.4.0

- `--field NAME` exists on every command; a product option spelled
  `--field` must have the trunk's shape and meaning, or be renamed. No
  other change: a product that never uses `--field` is unaffected.

## Egress (`cli/catalog.c` and `cli/maelys-egress.c`)

Egress reads its configuration and its secrets: since 0.5.11 both go through
`maelys_cli_read_trusted_file()` with the requirements they need
(`MAELYS_CLI_FILE_OWNER_CALLER | MAELYS_CLI_FILE_PRIVATE |
MAELYS_CLI_FILE_NO_SYMLINK | MAELYS_CLI_FILE_SINGLE_LINK` for a secret),
`maelys_cli_zero()` before releasing a secret, and `maelys_cli_fail_file()`
for the error, instead of a `maelys_cli_check_file()` followed by a read and
a product-side errno table.

Egress is the simplest consumer and the one whose migration is a contract
change rather than a code change: its 0.11 CLI reimplemented the same model
under the same `agent-cli/v2` name with a different vocabulary. The
reference vocabulary is the one of Maelys Git and Hermes, which `maelys-cli`
implements; Egress aligns on it in its next minor.

| Egress 0.11 | `maelys-cli` | Note |
| --- | --- | --- |
| `describe` member `path` | `pattern` (array of words) | `id` is added: dotted, stable |
| `usage` | `usage` and `input.synopsis` | identical strings, derived from the catalog |
| `summary` | `purpose` | operands and options keep `summary` |
| `effect`, `outputMode` | same names, same values | `protocol` added for stream commands |
| no `input`, no `outputSchema` | `input.operands`, `input.options`, `input.constraints`, `outputSchema`, `exitCodes` | additive |
| error codes in kebab case (`invalid-argument`) | `VALIDATION_FAILED`, `NOT_FOUND`, ... | the eleven stable codes of `command-conventions.md` |
| exit `2` = invalid invocation | exit `1` = any failure, `2` = a validation report with violations | `2` never accompanies an error envelope |

Steps, in one Egress change:

1. rewrite `cli/catalog.c` with the `MAELYS_CLI_*` macros and move each
   output schema to `schemas/*.json` embedded by `maelys-cli-embed`;
2. replace the hand-written parser and renderer of `cli/maelys-egress.c`
   with `maelys_cli_main()`; `serve` becomes
   `MAELYS_CLI_PROTOCOL_STREAM(..., "egress-fd4")` or the relevant name;
3. delete `tools/generate_cli_reference.py`, `tools/check_cli_contract.py`
   and any local reference-check target: the release socle finds the
   framework's generator by itself at the pinned commit and regenerates and
   compares `docs/cli.md`/`docs/cli-contract.json` (`maelys-release check`,
   `check-product.yml` in CI), declared in `docs/cli.reference` when Egress
   builds outside `build/bin` or documents more than its own binary;
4. replace `docs/command-conventions.md` and `docs/agent-cli.md` with the
   short product templates installed under
   `PREFIX/share/maelys-cli/templates/`, keeping only Egress specifics
   (the lifecycle stream of `serve`, `config validate` as the exit-2
   report) and linking the framework documents;
5. update the shell test and the skill for the new codes and exit semantics;
6. record the contract change in the Egress changelog as a breaking 0.x
   change and bump the minor.

Static archive hygiene: `libmaelys_egress.a` must not absorb
`libmaelys_cli.a` nor `libmaelys-json.a`; its pkg-config file declares
`Requires: maelys-cli` (and `maelys-json` only if Egress reads untrusted
JSON itself), so a product that reuses the Egress library and maelys-json
links each archive once.

Pinning: Egress pins `maelys-cli` by tag through the same
pinned-checkout scheme it uses for `maelys-system`; the framework's CI
(`.github/workflows/ci.yml`) runs the full check on Linux amd64/arm64 and
macOS before a tag is published.

## Hermes

Hermes stays TypeScript; it already implements the same contract. Its
command-contract skill was the model of `share/agents/claude-skill.md`.
`maelys agents install` can be run on Hermes' consumer repositories without
conflict: the block markers differ (`maelys-cli` versus `yavena-hermes`).
