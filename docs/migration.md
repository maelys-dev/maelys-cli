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
## Every consumer, at the help that fits eighty columns

- The text of `help` changes, in all its forms; `describe` does not. A
  product test that compared a line of `help` compares the new one, or
  better, reads `describe`.
- `PROGRAM help` no longer carries the usage of every command: it names
  each by its pattern and its purpose. The usage is in `help COMMAND_ID` and
  in the new `help FAMILY` / `FAMILY --help`, which lists the commands under
  an identifier. Nothing to declare; identifiers that share a prefix are
  what makes a family.
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
