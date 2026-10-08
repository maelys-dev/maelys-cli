# libmaelys_cli implementation notes

The contract every command follows is `agent-cli/v2`, specified in
[maelys-dev/agent-cli-spec](https://github.com/maelys-dev/agent-cli-spec)
(`spec/agent-cli.md` and `spec/extensions.md`, pinned in
`dependencies/agent-cli-spec.pin`). The specification defines discovery
(`describe` in its three forms), the descriptor, the value kinds, the six
effects and the plan/`--apply` transaction, the global options, the
built-in commands, the envelopes, the exit codes and the stable error
codes, protocol streams and delegates. Its conformance kit runs on
`maelys-hello` and the `maelys` dispatcher in `make check`. Where this
document and the specification differ, the specification wins and this
document is corrected.

This document only records how `libmaelys_cli` implements that contract:
what the catalog declares, what the parser enforces before a handler runs,
and what a change must prove. It is normative for products built on the
framework.

## The catalog is the single source

The catalog (`maelys_cli_command_t[]`, declared with the `MAELYS_CLI_*`
macros) is the executable source of truth: the parser, `help`, `describe`,
completion, the tests and any generated reference depend on it. Nothing
may maintain a second usage string; `usage` and `input.synopsis` are
derived from the declaration (required options first, then optional ones,
each in declaration order; choices without a `value_name` render as `a|b`).

Value kinds map one to one onto the specification: `MAELYS_CLI_FLAG`
(`boolean`), `_STRING`, `_INTEGER`, `_UNSIGNED`, `_SIZE`, `_DURATION`,
`_PATH`, `_ABSOLUTE_PATH`, `_CHOICE`, `_HEX` and `MAELYS_CLI_HEX_OR` (`hex`, one or
two accepted lengths), `_DIGEST` (`ALGORITHM:HEX`). Ranges, choices and digit
counts are enforced by the parser; a handler never re-validates a shape a
kind expresses.

Operands may be typed like option values (`MAELYS_CLI_OPERAND_CHOICE`,
`MAELYS_CLI_OPERAND_KIND`, or `MAELYS_CLI_OPERAND_OPTIONAL` plus `.kind`);
handlers read them through `maelys_cli_operand_choice()`,
`maelys_cli_operand_unsigned()` and `maelys_cli_operand_integer()`. An
operand describes its value exactly as an option's argument does (spec 2.6):
`.hex_digits` and `.hex_digits_alternative` are its `digits`, a digest's
`.choices` its `algorithms`, and `.pattern` — on a string or path kind,
compiled at startup, enforced by the parser — its `pattern`.

Option constraints: `depends_on` (one option), `depends_on_all` (every
listed option), `conflicts_with` (an option, or an operand named by its
UPPER_CASE placeholder, as `--prefix` conflicts with `COMMAND_ID`), and
`group` (all-or-none). They are exposed as `requires`, `conflictsWith`,
`group` and `input.constraints` entries of kind `requires`, `at-most-one`
and `all-or-none`; an all-or-none entry carries no name (spec 2.5), its
options being the whole rule, and the kit checks that the entries and the
`group`s of a command agree.

A command states the rules its option fields cannot say through
`MAELYS_CLI_CONSTRAINTS(array)` of `MAELYS_CLI_CONSTRAINT(kind, options)`
entries, `options` a NULL-terminated list of at least two of its own option
names: `MAELYS_CLI_CONSTRAINT_EXACTLY_ONE`, which has no option-level form
and for which `input.constraints` is the only site — five sources of
policy, exactly one of them, zero refused as two are;
`MAELYS_CLI_CONSTRAINT_AT_MOST_ONE` over more than the pair
`conflicts_with` expresses; `MAELYS_CLI_CONSTRAINT_REQUIRES`, the first
option requiring every other. Each is validated at startup (known options,
no duplicate, at least two) and enforced by the parser in the same causal
slot as the dependencies. `MAELYS_CLI_CONSTRAINT_ALL_OR_NONE` is refused
there: all-or-none is declared by `.group`, and a rule has one declaration
so that `describe` and the parser cannot drift apart.

`.pattern` is, as `argument.pattern`, the regular expression a string or
path value must match (spec 2.3): the parser enforces it with POSIX ERE
(`regcomp`, `REG_EXTENDED`) and refuses a mismatch with `VALIDATION_FAILED`;
the catalog validation refuses a pattern that does not compile. Write
patterns in the common subset of ECMA-262 and POSIX ERE: literals, bracket
classes and ranges, `.`, `*`, `+`, `?`, `{m,n}`, alternation, plain
`( )` groups, `^` and `$`; no `(?:`, no `\d`, no lookaround, no
back-reference, so that an agent, the kit, the C parser and the Python
module read the same motif.

`.hidden` on an option (spec 2.2) marks a diagnostic or trial option that
humans are not offered: the parser accepts it and applies its constraints,
`describe` lists it with `hidden: true` (emitted only when true), and the
derived synopsis, `help COMMAND_ID` and `__complete` leave it out. A hidden
option is never required; the catalog validation refuses that combination.

`default_text` is the single source of an option's default: the catalog
validation checks it against the option's kind at startup, and the typed
accessors return it when the option is absent; `MAELYS_CLI_DEFAULT_OF`
takes it from a constant of the product library.

A command that a build cannot provide declares `.unavailable = "reason"`
instead of a failing handler: it stays in `describe` with
`available: false` and fails with `UNSUPPORTED`, or with
`.unavailable_code` when absence is not the cause — a component that does
not match its declared digest is `ACCESS_DENIED`, one that is gone is
`NOT_FOUND`. The code must be one of the stable eleven, and needs the
reason beside it; an agent reads the cause from the code and never from the
sentence. A product with build
variants composes its catalog at startup with
`maelys_cli_catalog_concat()`: a later part may replace an `.unavailable`
descriptor of the same identifier in place, and nothing else (`EEXIST`).

A stream command names the protocol that owns its stdio through
`.protocol` (`MAELYS_CLI_PROTOCOL_STREAM`), exposed as `protocol` next to
`outputMode: "protocol-stream"`; a delegate (`MAELYS_CLI_EXTERNAL`) relays
a child without a named protocol and has no `protocol` member.

## What the parser enforces, in causal order

1. command resolution (`INVALID_COMMAND`);
2. option spelling, support by the command, duplication (unless
   `repeatable`);
3. option value kind, range, choice;
4. option dependencies (`depends_on`, `depends_on_all`, `group`),
   conflicts (`conflicts_with`) and the command's stated constraints
   (`exactly-one`, `at-most-one`, `requires`);
5. required options;
6. operand arity and typed operands;
7. rendering constraints: stream commands refuse rendering options, `jsonl`
   is accepted only by `json-records` commands.

Everything after that belongs to the handler: file type and permissions,
syntax, schema, policy, current state and concurrency preconditions,
reported with the error code of the boundary that actually failed.
Configuration, manifests and secrets are read with
`maelys_cli_read_trusted_file()`, which applies the trust requirements to
the descriptor it reads and bounds the read by the bytes read, so a link
posted between a check and a read, a file that grows meanwhile or a FIFO at
the path cannot change what is judged; `maelys_cli_check_file()` remains for
a file that is not read, such as an executable. `maelys_cli_fail_file()`
turns the errno and explanation they leave into the stable code:
`NOT_FOUND`, `ACCESS_DENIED`, `VALIDATION_FAILED` or `IO_FAILED`.

An option value remains a value even when it starts with `--`; `--` ends
option parsing; delegate commands receive every argument after their pattern
verbatim, including `--help`. A delegate script names its interpreter directly
with an absolute shebang; relative interpreters and interpreters named `env`
are refused because they perform implicit current-directory or `PATH` lookup.

The trunk options of spec 2.3 exist on every command without a declaration
and appear in `globalOptions`: `--progress auto|always|never` (progress of
a long run on stderr, in text mode, `auto` only when stderr is a terminal),
`--verbose` (details on stderr, text mode, one `PROGRAM: ` line each) and
`--pager auto|always|never` (the text rendering through the user's pager
when stdout is a terminal; never in a pipe, in JSON or JSONL, or under
`--non-interactive`). None is repeatable; `--flag=false` disables a flag.
`--pager` is a rendering option, refused by a `protocol-stream` command;
`--progress` and `--verbose` are not, a stream accepts them and a delegate
receives all three verbatim. Under `--format json` or `jsonl` the three are
accepted and write nothing. The pager is `PAGER`, split with POSIX quoting
and no expansion (empty disables it), or `less` with `LESS=FRX`; a pager
that cannot start leaves the rendering on stdout.

`--field NAME` (spec 2.4, also a rendering option) renders one top-level
member of `data` instead of the whole result, by the tab-separated-records
rules above extended to every JSON shape: an array whose elements are all
objects renders one row per object; any other array is one value per line;
an object is one row, its members as columns; any other scalar is its
escaped value on one line. In `jsonl` mode an array is one compact value
per line and anything else is exactly one line, so the rendering is total
and never depends on what the handler returned. `--field` conflicts with
an explicit `--format json`/`--json` (a filtered envelope would not
validate against `outputSchema`); the parser refuses the pair when both
are explicit, and the same refusal comes before the command runs when
`MAELYS_CLI_FORMAT=json` resolves the format after parsing: a rendering
refusal never follows a write. For the same reason a command that can
write -- a transaction, with or without `--apply`, or an `execute` --
accepts only a name listed in the top-level `required` of its
`outputSchema`, and refuses any other before it runs (spec 2.9): a member
the schema leaves optional is refused even when this run would have
carried it, and a schema that requires none accepts no `--field`. List in
`required` the members a transaction always returns and that a caller may
want to read. On a `read`, a name absent from `data` is
`VALIDATION_FAILED`, found once the handler has produced `data`: a refusal
costs nothing there. `--field` also lifts the
records-only restriction on `jsonl`: combined with `--field`, `jsonl` is
accepted on any command.

Text records into a pipe render one tab-separated row per record (spec 2.3,
section 7): the columns are the union of the records' member names sorted
by code point, a missing member is an empty field, a string is unquoted
with `\\`, `\t`, `\r`, `\n` and `\uXXXX` escapes, every other value is
compact JSON. On a terminal the `human_line` given to
`maelys_cli_emit_record()` is shown instead when there is one. The stable
machine form stays `jsonl`, whose lines are written once the command has
succeeded: a command that fails after emitting records leaves stdout empty,
as in every format.

`--apply` plans again and applies that plan: the re-validation says the
state still allows the transaction, not that the action is the one the
caller reviewed. A transaction whose plan has a stable identity binds the
two with the reserved form of spec 2.9, section 4, and no other:
`MAELYS_CLI_EXPECT_OPTION` beside `MAELYS_CLI_APPLY_OPTION`, and
`fingerprint` in the `required` of its output schema. The plan returns
`data.fingerprint`, a `sha256:HEX` over the action and over the state of
the resources it would touch, built with `maelys_cli_fingerprint_*()`; the
handler calls `maelys_cli_expect()` before its first write, and `--apply
--expect FINGERPRINT` fails with `PRECONDITION_FAILED` when the plan is no
longer that one, nothing having been written. What the fingerprint covers
is the product's decision, and the decision that matters: the same writes
on the same state must give the same string, another write or another
state another. After an `--apply` whose outcome is unknown, a new plan
answers: the same fingerprint, the action is still to be done. The
fingerprint narrows the window between the review and the write; closing
it is the product's locking. `note write` of `maelys-hello` is the worked
example.

`--dry-run` and `--plan` are refused, with the migration hint, only on
commands that declare `--apply`; a product without transactions is not
affected. The refusal is deliberate: accepting an alias would let two
spellings of the same intent coexist across products, which is what the
shared contract exists to prevent.

## Help

`help` is read by a person, in a terminal, and is laid out for one. It is
rendered at the width of the terminal when stdout is one, between 60 and
100 columns, and at 80 anywhere else: what goes into a pipe, a file or
`data.text` does not depend on a window. A description stands beside a
label that fits its column and below one that does not; a line breaks
between words, a usage between its groups, never inside `[--option VALUE]`;
widths are counted in columns, an accented letter being one and a CJK
character two.

Four forms, all generated from the catalog:

- `PROGRAM help` names each command by its pattern and its purpose, the
  product's commands first, then the ones every program has. It does not
  repeat every usage: a catalog of forty commands stays a screen. What every
  program has in common is named there and not repeated: the global options
  by their names, the agent contract by its first rule.
- `PROGRAM help conventions` has that common part whole: each global option
  with what it does, and the agent contract. A product's own command or
  family of that name is shown instead; the topic takes no identifier away.
- `PROGRAM help COMMAND_ID`, or `PROGRAM COMMAND --help`, gives one command:
  usage, purpose, effect, output mode, operands, options.
- `PROGRAM help FAMILY`, or `PROGRAM FAMILY --help`, gives a family: the
  commands under an identifier (`note` holds `note.write`), which is the
  namespace `describe --summary --prefix` selects, each with its usage and
  its purpose below. Words that name no command are still an error without
  `--help`.

A product writes none of this and must not: a purpose that reads well in one
line, and identifiers that share a prefix when the commands form a family,
are what the layout needs.

## Rendering decisions

`MAELYS_CLI_FORMAT=json|text` in the environment selects the default
format: `--format` and `--json` override it, `--compact` and `--pretty`
select none and leave it in force. For a stream command it only shapes the
failure envelope on stderr, since stdout belongs to the protocol.
`--non-interactive` guarantees that no question is asked:
`maelys_cli_confirm()` fails with `VALIDATION_FAILED` instead.

Text rendering of a failure is `PROGRAM: [CODE] message` followed by
`Hint: ...` when present, colored on a terminal unless `--color never`,
`NO_COLOR` or `TERM=dumb` applies. Envelope keys are written in a fixed
order (`schemaVersion`, `contract`, `command`, `ok`, `exitCode`, then
`data` or `error`), and `describe COMMAND_ID` is minimal: `globalOptions`,
`output` and `invariants` belong to the inventory forms only.

Framework diagnostics escape control bytes from arguments as `\\n`, `\\r`,
`\\t` or `\\xNN` in text mode; Unicode line and bidirectional controls use
`\\uNNNN`, and malformed UTF-8 bytes use `\\xNN`. JSON mode uses JSON
escaping. Products use `maelys_cli_warn()` for the same terminal-safe behavior.

`ACCESS_DENIED` also covers an untrusted file or binary (ownership, modes,
the trust of the directory it resolves to, digest), and `PROTOCOL_FAILED` a
manifest that violates `maelys.cli-extension/v1`. `UNEXPECTED` is what a handler that never
replies, or replies with invalid JSON, produces.

Administrative paths are absolute when they become durable configuration.
Secrets are regular non-symlink files owned by the caller with no group or
world permissions (`MAELYS_CLI_FILE_PRIVATE`). Every file write names
`MAELYS_CLI_WRITE_REPLACE` or `MAELYS_CLI_WRITE_NO_REPLACE`; an existing
target is never replaced implicitly.

## Shell completion

`PROGRAM completion bash|zsh|fish` prints a script that calls the hidden
`PROGRAM __complete -- WORDS...` at every completion, and writes nothing.
`__complete` is the oracle and the script a rendering of it (agent-cli/v2
2.7, section 6): the script offers the words `__complete` returns, no
others, and falls back to the shell's file completion when it returns none.

Candidates are derived from the catalog, in catalog order: command words,
each once; after `--`, the options not yet given, a repeatable one again;
`--option=choice`; the choices of an option's argument or of a typed
operand; digest algorithm prefixes; command identifiers after `help` and
`describe`. A path, a free-text value and an operand declared without a kind
return nothing, which is what sends the shell to the files. Stream commands
never offer rendering options; a hidden or unavailable command is
never offered, as a word or as an identifier.

After the pattern of an external command the words are that command's own:
the program forwards the words to its `__complete` and returns what it
answers as records, the same in every format, and none when it is not
installed. The Python module holds no delegate executable and returns none
there.

The C library and the Python module return the same words in the same
order, and print the same three scripts when both call `__complete`;
`make hello-parity-check` compares both and `make completion-check` drives
the scripts in every installed shell, `/bin/bash` included, which macOS
keeps at 3.2. The zsh script works sourced after `compinit`
(`source <(PROGRAM completion zsh)`) and autoloaded from `fpath` as
`_PROGRAM`.

A Python program prints by default a script that carries the candidates of
its catalog and its `version`, and launches no process at a Tab: its
interpreter costs at every launch what a C program does not. A C program,
which answers `__complete` in under 10 ms, keeps the script that calls it.
`docs/python.md` says how such a script is renewed and when a program
declines it.

## Proof of implementation

Any command change updates, in the same change:

1. the catalog entry, declared with the `MAELYS_CLI_*` macros;
2. the handler consuming the validated invocation;
3. the JSON Schema file when the data shape changes (embedded with
   `maelys-cli-embed`, referenced with `MAELYS_CLI_SCHEMA`);
4. the tests of the catalog, options and envelopes;
5. the generated reference (`maelys-cli-reference`), regenerated and
   checked by the release socle (`maelys-release check .`), not by a
   target of this repository; `docs/cli.reference` declares only what the
   socle cannot guess, `[build]` and the second program documented.

`maelys_cli_catalog_validate()` runs at every startup and in tests. It
checks identifier and pattern validity and uniqueness, summaries, value
declarations and defaults, `depends_on`/`depends_on_all`/`conflicts_with`
targets and groups, schema JSON validity, the synopsis length, the presence
of `--apply` on transactions and the handler-or-delegate-or-unavailable rule.
The conformance kit of the specification then proves, from the outside,
that the binaries speak `agent-cli/v2`.
