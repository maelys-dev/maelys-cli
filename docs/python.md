<!-- VERIFIED by make python-doc-check against python/maelys_cli.py; edit with the code. -->
# The Python framework: `python/maelys_cli.py`

`python/maelys_cli.py` is the Python counterpart of `libmaelys_cli`: one
file, standard library only, Python 3.9 or later, implementing the
[agent-cli/v2](agent-cli.md) contract for a product written in Python. A
product pins maelys-cli through `dependencies/maelys-cli.pin` as a C product
does, clones it with `scripts/checkout-dependency.sh maelys-cli`, and either
copies `python/maelys_cli.py` next to its program or adds `python/` to
`sys.path`. The reference product is `python/examples/hello.py`; the
conformance kit of maelys-dev/agent-cli-spec runs against it in `make check`.

## Declaring a program

```python
import maelys_cli as cli

def greet(invocation):
    name = invocation.operands[0]
    times = invocation.option("--times")          # typed, or the catalog's default
    greeting = ("Hello, " + name + "!").upper() if invocation.flag("--shout") else "Hello, " + name + "!"
    return {"greeting": greeting, "lines": [greeting] * times}, cli.EXIT_OK

PROGRAM = cli.Program("hello", "Hello", "0.1.0", [
    cli.read("greet", "greet", "Greet someone.", greet,
             operands=[cli.operand("NAME", "Person or system to greet.")],
             options=[cli.flag("--shout", "Render the greeting in capitals."),
                      cli.option("--times", "Repeat the greeting.",
                                 cli.argument("N", "unsigned", minimum=1, maximum=10), default="1")],
             schema={"type": "object", "required": ["greeting", "lines"]}),
])
sys.exit(PROGRAM.main())
```

A handler receives an `Invocation`: `invocation.operands` (typed when the
operand declares a kind), `invocation.option("--name")` (the typed value,
else the catalog's default parsed with the same kind, else `None`),
`invocation.flag("--name")`, `invocation.apply`, `invocation.format`,
`invocation.non_interactive`; `parse_value(kind, text, spec, where, usage)`
is the validator behind both and answers `VALIDATION_FAILED` with the
expected shape.

`Program(program, product, version, commands, guide=..., text=...)` adds
the built-ins `help`, `version`, `describe`, `completion` and
`complete.candidates` (`__complete`), checks that every `requires` and
`conflicts_with` entry names an option or an operand of the command, and
refuses a duplicate identifier. The catalog is the only source of the
synopsis, the help, `describe` and the completion.

## Binding a plan to its application

`cli.transaction(..., expect=True)` declares the reserved `--expect
FINGERPRINT` (spec 2.9, section 4; `MAELYS_CLI_EXPECT_OPTION` in C): a
sha256 digest that requires `--apply`. The `schema=` must then list
`fingerprint` in its `required`, or the declaration is refused, as is an
`--expect` of another shape on a transaction.

```python
def note_write(invocation):
    fingerprint = cli.Fingerprint().add("path", path).add("content", content) \
        .add_file("target", path, 1 << 20).finish()
    invocation.expect(fingerprint)          # before the first write
    plan = {"mode": "plan", "path": path, "fingerprint": fingerprint}
    ...
```

`Fingerprint` builds the `sha256:HEX` string over the action and over the
state of the resources it would touch: `add(label, value)` takes bytes, a
string or `None` for an absent value, `add_file(label, path, maximum_size)`
the state of a file (absent, or the sha256 of its content; any other
`OSError` is raised), `finish()` returns the string. It frames its entries
as `maelys_cli_fingerprint_*()` does, so a C and a Python product that add
the same entries return the same fingerprint. `invocation.expect(fingerprint)`
returns when `--expect` was not given or names it, and raises
`PRECONDITION_FAILED` otherwise; a handler that answers when `--expect` was
given without having called it is answered `UNEXPECTED`. A handler that
derives what it writes from what the file holds adds the bytes it read with
`add(label, value)`, not `add_file`, which reads the path again; and a
replacing write still replaces a content written after the fingerprint's
read (`docs/command-conventions.md`, on the window `--expect` leaves).
## Examples

`examples=[cli.example("note write /tmp/note.txt --content hello", "Plan the
note.")]` on any declaration gives a command its examples (spec 2.12,
section 2; `MAELYS_CLI_EXAMPLES` in C). `example(words, summary)` takes the
command line without the program's name, starting with the command's
pattern, its words separated by single spaces, and one sentence. `Program`
parses each example when it is built and raises `ValueError` when the
command does not accept it, or when it shows a hidden option or asks for
`--help`; nothing runs it. Write a word as the program receives it, without
shell quoting: the help quotes the words a shell would read otherwise. `help
COMMAND_ID` shows them, `describe` lists them except in its summary. The rules are those of
`docs/command-conventions.md`, "Examples".

## A default command

`Program(..., default_command="show")` names the command a line that names
no command runs (spec, section 8; `.default_command` in C): a line whose
first word is an option, `--` or a word with one dash, and an empty line.
Its operands are then written after `--`, and a bare word before `--` is
refused in its name. `Program` raises `ValueError` when the identifier is
not a command of the program, is a built-in, hidden, unavailable or a
delegate, or is an `execute` command. `describe` carries `defaultCommand`,
`help` names it, and the completion script is the one that calls
`__complete`. Without it nothing changes; with it, `PROGRAM --json list`,
which ran `list`, selects the default command and is refused: `PROGRAM list
--json` runs `list`. The rules are those of
`docs/command-conventions.md`, "A default command".

## Help

`Program(..., help_palette={"heading": "1;34", "command": "36"})` colors
the help role by role (C: `maelys_cli_help_palette_t`): the roles are
`HELP_PALETTE_ROLES`, `heading`, `command`, `option`, `placeholder` and
`example`; each value is SGR parameters, digits and `;` only. A role left
out or `None` keeps the framework's choice, bold headings and nothing else;
`""` shows it plain. `Program` raises `ValueError` on an unknown role or a
value that is not SGR parameters. Color is shown only where the stream
accepts it (`invocation.color_stdout`), and `data.text` never carries it.

`help`, `help COMMAND_ID`, `help FAMILY` and their `--help` spellings are
generated from the catalog and laid out as the C library lays them out,
byte for byte (`docs/command-conventions.md`, "Help"): within 80 columns
where stdout is no terminal, the terminal's width between 60 and 100 where
it is one. `program.guide(width)`, `program.command_help(command, width)`
and `program.family_help(name, commands, width)` return the three texts;
`help_width(fmt)` is the width the built-in uses and `display_width(text)`
counts columns, an accented letter being one, a CJK character two and a
combining mark none.

## Languages

`Program(..., translations={"fr": {"Greet someone.": "Saluer quelqu'un.",
"Nobody is called %s here.": "Personne ne s'appelle %s ici."}})` declares
the product's languages besides English (C: `maelys_cli_app_t.languages`),
each a table from a text exactly as the catalog writes it to its
translation, at most `MAX_LANGUAGES` (32). `canonical_language="fr"` says the catalog is
written in French (C: `maelys_cli_app_t.canonical_language`), English
(`CANONICAL_LANGUAGE`) otherwise; `version` names it as `lang`, the tables
translate from it, and the framework's texts follow it. `Program` raises
`ValueError`, with the message of the C catalog validation, on a tag that
is not `language[-Script][-REGION]` (`LANGUAGE_TAG_PATTERN`), the canonical
language, a language declared
twice, an empty text or one with a control character other than a newline,
and a translation that does not keep the printf conversions of its source in
order.

The language is resolved as in C (`docs/command-conventions.md`, "Languages
of human text"): `--lang TAG` (`invocation.lang`), then `MAELYS_CLI_LANG`,
then `LC_ALL`, `LC_MESSAGES` and `LANG`, the first one set deciding alone,
looked up among the declared languages, English otherwise. The help,
`data.text` of `help` included, the text rendering of a failure, and
`invocation.detail()`, `invocation.show_progress()` and `program.warn()` are
written in it; `describe`, envelopes and records never change. The
framework's own texts come from the same `i18n/*.tsv` as the C library's.

`failure(code, template, *arguments, hint=...)` returns a `Failure` whose
message is `template` with its printf conversions -- `%s`, `%d`, `%zu`,
`%llu` as C writes them -- filled by `arguments`, and which keeps the
template and the text of each argument: the text rendering puts them back
in the template's translation. Raise what it returns. A `Failure` built
with a message already assembled is translated only when a table names that
message whole. `detail`, `show_progress` and `warn` take a template and its
arguments the same way; without arguments their text is not formatted.

## Completion

`PROGRAM completion bash|zsh|fish` prints a script that **carries the
candidates of the catalog** and launches no process at a Tab. A Python
program pays its interpreter at every launch, about 90 ms for `hello.py`,
which a completion that calls `__complete` pays at each Tab; the script that
carries its candidates answers in about 5 ms under bash. It is `__complete`
written in each shell over one table of the catalog: it offers the words
`__complete` returns for every word list and falls back to files when there
is none, which `make completion-check` proves in the three shells. After the
pattern of a `cli.external` command, whose words the catalog does not hold,
it calls `PROGRAM __complete`.

The script names the catalog's `version` in its first line and is current
while that catalog is unchanged. How an upgrade renews it depends on how it
was installed, and the framework watches no file date:

- `source <(PROGRAM completion bash)` in a startup file regenerates it at
  every shell, for one launch per shell rather than one per Tab;
- a package writes the file when it is built and replaces it with the
  program;
- a file someone wrote by hand is rewritten by hand after an upgrade, and
  its first line says which version it carries.

`Program(..., static_completion=False)` keeps the script that calls
`__complete` at every completion. Declare it when the catalog depends on the
machine the program runs on -- commands made `unavailable` by what is
installed, say: a script generated where a package was built would carry
that machine's catalog under the same version. A catalog holding a word the
static form cannot carry (anything outside letters, digits and
`._:/+@%,=-` in a pattern, an option, a choice or an algorithm) gets that
script too, by itself. `program.completion_script(shell, static=None)`
returns either text without running the command.

| Declaration | Effect | What the handler returns |
| --- | --- | --- |
| `cli.read(id, pattern, purpose, handler, ...)` | `read` | `(data, EXIT_OK)` or `(report, EXIT_VIOLATIONS)` |
| `cli.records(...)` | `read`, `json-records` | `({"count": N, "records": [...]}, EXIT_OK)`; `--format jsonl` accepted |
| `cli.transaction(..., commit=False, expect=False)` | `{"plan": "preview", "apply": "apply"}` | `data.mode` is `plan` without `--apply`, `apply` with it; `--dry-run` and `--plan` are refused |
| `cli.execute(...)` | `execute` | `(data, exit_code)` |
| `cli.stream(..., protocol=...)` | `stream`, `protocol-stream` | the exit status; rendering options are refused, stdout belongs to the protocol |
| `cli.external(...)` | delegate, `passthrough` | the exit status; every word after the pattern reaches the handler verbatim |

`synopsis="adopt DIR [--apply]"` overrides the derived usage, as
`.synopsis` does in C, for a command whose trial or rarely used options
should not clutter the line; it must start with the pattern, the catalog
still declares every option, `describe` still lists them all and
`input.synopsis` stays equal to `usage`.

A command this build cannot run declares `unavailable="reason"`: it is
listed by `describe` with `available: false`, refused with `UNSUPPORTED`,
and never completed. `unavailable_code="ACCESS_DENIED"` names the code it
answers instead, for a cause that is not absence (as `.unavailable_code`
does in C); it must be one of the stable codes and needs the reason beside
it. `hidden=True` keeps it out of `help` and the completion.

## Operands, options and value kinds

`cli.operand(name, summary, required=True, variadic=False, kind=None,
choices=None, minimum=None, maximum=None, algorithms=None, pattern=None,
digits=None)`; an operand describes its value exactly as an argument does
(spec 2.6), and `pattern` is refused on a kind that is not `string` or
`path`, `digits` on a kind that is not `hex`; at most one
operand is variadic, and it is the last one. `cli.option(long, summary, argument=None,
default=None, required=False, repeatable=False, requires=(),
conflicts_with=(), group=None)`; `cli.flag(long, summary, ...)` is an
option without argument. Explicit flags accept `true/false`, `yes/no`,
`on/off` and `1/0`; any other value is refused, including on `--apply`.
`cli.constraint(kind, *options)`, passed to a command as `constraints=`,
states a rule the option fields cannot say (spec 2.5): `exactly-one` —
which has no option-level form, so `input.constraints` is its only site,
and which refuses zero of the options as it refuses two — `at-most-one`
over more than a pair, and `requires`, the first option requiring every
other; at least two distinct options of the command, checked when the
`Program` is built. `all-or-none` is refused there: it is declared with
`group=`, its option-level form, so a rule has one declaration.
`cli.argument(name,
kind, choices, minimum, maximum, algorithms, pattern, digits)` declares the
value. Its `name` is the placeholder of the usage and of help; `VALUE` is
the name of a value that declares none, as describe gives it for a C option
without `.value_name`: a `choice` named `VALUE` is spelled by its choices
(`--level low|high`), a `digest` named `VALUE` is `ALGORITHM:HEX` in the
usage, and any other name is written as declared (`--level LEVEL`).
A `pattern` on a `string` or `path` argument is enforced (agent-cli/v2,
section 3): the value must match it (a search, `^` and `$` at the ends of
the value), else `VALIDATION_FAILED`. It is written in a subset that
ECMA-262 (`u` flag) and POSIX ERE read the same way, which `Program` reads
as the C library does and refuses outside of, with the reason: only
printable ASCII, no `(?:`, no `\d`, no POSIX class, no lazy `*?`, a bound
`{m,n}` of at most 255. `.` is any character but the four line terminators
of ECMA-262 (U+000A, U+000D, U+2028, U+2029). A value is searched as in C, by a Thompson
simulation of the same program, never by `re`, whose backtracking takes
exponential time on a pattern the subset admits (`(a+)+b`): the work is
O((len(value) + 1) x states), the memory O(states), states being at most
65536. `c/tests/patterns.tsv` is the corpus C and Python both answer.

`hidden=True` on an option or a flag keeps it out of the synopsis, the help
and the completion while `describe` lists it with `hidden: true` and the
parser accepts it, as `.hidden` does in C; a hidden option is never
required. `invocation.program.warn(message)` writes `program: warning:
message` on stderr, as `maelys_cli_warn()` does.

The kinds are those of the contract, validated before the handler runs and
returned typed by `invocation.option()` and `invocation.operands`:
`boolean`, `string` (optionally `pattern`), `integer` and `unsigned`
(ranges), `size` (`4K`, `16M`, `2G`, `1T`, in bytes), `duration` (unit
required: `ms`, `s`, `m`, `h`, `d`; in milliseconds) — `describe` states a
bound of an unsigned kind only when it is declared, and a `minimum=0` on
`unsigned`, `size` or `duration` is the kind's own floor, not stated —,
`path`,
`absolute-path`, `choice`, `hex` (`digits` states its width: an integer,
or a pair such as `[40, 64]` when two widths are accepted, as
`MAELYS_CLI_HEX` and `MAELYS_CLI_HEX_OR` do; a `hex` without `digits`, or
with `minimum`/`maximum`, is refused), `sha256`, `digest` (`ALGORITHM:HEX`
with `algorithms`). A `default` is
text, parsed with the same kind when the option is absent: a handler never
repeats a default. A repeatable option yields a list. A `pattern` on a
`string` argument documents the value in `describe`; the parser does not
enforce it, as the C library does not (the built-in `--prefix` validates
its own grammar and answers `VALIDATION_FAILED`).

Errors are reported in the contract's causal order: unknown command;
option spelling, support and duplication; value kind, range and choice;
`requires`, `conflicts_with`, all-or-none `group`s and the stated
`constraints`; required options; operand arity and kinds; availability;
rendering constraints. `--help` is answered after the value checks and
before the rest (`docs/command-conventions.md`, "What the parser enforces").
Each refusal of the line carries in `Failure.issues` one entry, a reserved
code and the path at fault (`{"code": "VALUE_RANGE", "message": ...,
"path": "--count"}`), as C's does (agent-cli/v2 2.23.0, section 8).
Inside the handler, raise
`cli.Failure(code, message, hint)` with one of the eleven stable codes.
`cli.Failure(code, message, hint, issues=[...], extensions={...})` adds the
detail the envelope schema admits (C: `maelys_cli_fail_error_extended`):
`issues`, each `{"code", "message"}` with an optional `"path"`, string or
list of strings and integers, is the cause under the code -- cx's
`VALIDATION_FAILED` with `{"code": "PANES_CONFIG", "path": ["--config",
"panes", 0, "key"], "message": ...}`; `extensions`, each named per `EXTENSION_NAME`, is machine
detail, such as the revisions of a `REVISION_CONFLICT`. The code matches
`CODE_GRAMMAR`: a stable one, or one of the product's own domain, which it
documents. The text rendering is unchanged. A code, an issue or a member
that breaks the rule is a defect of the command, answered `UNEXPECTED`.

## Color

The C library resolves `--color` once, into a per-stream decision
(`maelys_cli_terminal_detect()`'s `color_stdout` / `color_stderr`), and a
handler reads it from the context; the Python module mirrors that instead
of a handler re-scanning `sys.argv` or the environment itself, which gets
the per-option spellings, `NO_COLOR`, `CLICOLOR_FORCE` and the per-stream
distinction subtly wrong. `invocation.color` is the raw choice (`auto`,
`always` or `never`); `invocation.color_stdout` and `invocation.color_stderr`
are the resolved booleans a handler builds its own colored human rendering
from, decided once from the real stdout and stderr (unaffected by later
paging, per section 5: color follows the original stdout). `terminal_color(mode)`
is the function behind them, `(color_stdout, color_stderr)` for one mode:
`never` wins; `always` or `CLICOLOR_FORCE` force both on; otherwise
`NO_COLOR` or `TERM=dumb` force both off; otherwise each stream follows its
own `isatty()`. The runtime uses the same resolution for its own failure
rendering; before a command resolves (a command line that fails to parse
at all), only an explicit `--color never` is honored, never an unresolved
`--color always` — the same asymmetry as `maelys_cli_run()`'s prescan.

## Diagnostics and paging

The trunk options of spec 2.3 exist on every command: `--progress
auto|always|never`, `--verbose` and `--pager auto|always|never`, listed in
`globalOptions`, never repeatable. A handler reads `invocation.verbose`
(true only in text mode) and `invocation.progress_wanted`, and writes with
`invocation.detail(message)` (one `PROGRAM: message` line on stderr),
`invocation.show_progress(message)` (transient on a terminal, erased by
`invocation.progress_done()`); all three are silent under `--format json`
or `jsonl`. `page_text(text, invocation)` sends the text rendering through
the pager named by `PAGER` (`pager_command()`, POSIX quoting, no
expansion, `less` with `LESS=FRX` when unset) when stdout is a terminal,
`--pager` is not `never` and `--non-interactive` is absent; `main()` calls
it, a pager never starts in a pipe, and a pager that cannot start leaves the
text on stdout. `record_text(records)` is the tab-separated form of records
(section 7 of the contract) used by text rendering of `json-records`
commands, into a pipe as on a terminal.

## Rendering

`--format json` renders the envelope on stdout; a failure is an envelope on
stderr and exit 1, or `PROGRAM: [CODE] message` plus `Hint:` in text mode.
`MAELYS_CLI_FORMAT=json` in the environment selects JSON by default, which is
how an agent obtains the failure envelope of a `stream` command; `text`
selects text and any other value is ignored, as in C. Text
rendering of success is the `text` mapping given to `Program` (`{"greet":
render_greet}`), one line per record for `json-records`, or the data as
indented JSON.

`Invocation.field` (spec 2.4's `--field NAME`) renders one top-level member
of `data` instead of the whole result: `field_text(value)` is the four-shape
dispatcher (array of objects through `record_text`, any other array one
value per line, an object as a single-element `record_text`, any other
scalar its escaped value); `field_jsonl(value)` is its `jsonl` counterpart
(an array as one compact value per line, else exactly one line). `main()`
refuses `--field` together with `--format json` (`VALIDATION_FAILED`,
before the handler runs, also when `MAELYS_CLI_FORMAT=json` resolved the
format); on a command that can write (a transaction or an `execute`), a
name the `required` of its `schema=` does not list, before the handler runs
too (spec 2.9); on a `read`, a name absent from `data`, after the handler
has run. `--field` also lifts the records-only restriction on `jsonl`.

## Stability of the module

The public API of `python/maelys_cli.py` is a contract, vendored byte for
byte by its consumers (maelys-release copies the file at the commit its
`dependencies/maelys-cli.pin` names and checks its SHA-256): the declaration
functions `read`, `records`, `transaction`, `execute`, `stream`,
`external`, `operand`, `option`, `flag`, `argument`, `constraint`;
`Program` and the
arguments of its constructor (`program`, `product`, `version`,
`commands`, `guide=`, `text=`, `framework=`) and `Program.main(argv)`;
the command keywords `operands=`, `options=`, `constraints=`, `schema=`,
`hidden=`, `unavailable=`, `unavailable_code=`, `synopsis=`, `protocol=`;
`Invocation` with `operands`, `raw_operands`, `options`, `option()`,
`flag()`, `apply`, `format`, `compact`, `non_interactive`, `program`,
`verbose`, `progress`, `pager`, `field`, `progress_wanted`, `detail()`,
`show_progress()`, `progress_done()`, `color`, `color_stdout`,
`color_stderr`; `Program.warn(message)`; `terminal_color`;
`record_text`, `pager_command`, `page_text`, `field_text`, `field_jsonl`; `Failure`;
the file and error functions above; the `EXIT_*` and `FILE_*` constants.
The rules are those of the C library: within the `0.5` line every change
is additive (a new keyword with a default, a new function, a new
constant); a signature or a behavior change, a removal, or a renamed
member is a `0.6`. Every release that touches the module says so in
`CHANGELOG.md` under a line starting with `python/maelys_cli.py`, naming
what changed, so a consumer knows whether to bump its pin and re-vendor.
Names starting with an underscore, `parse_value`'s message texts and
the exact text rendering are not part of the contract.

## Files and errors

The counterpart of `maelys/cli/files.h`, with the same requirements, the
same `errno` values and the same explanations, so a product written in
Python judges a file exactly as a C product does:

| Name | Contract |
| --- | --- |
| `FILE_REGULAR`, `FILE_NO_SYMLINK`, `FILE_OWNER_TRUSTED`, `FILE_OWNER_CALLER`, `FILE_NOT_WRITABLE_BY_OTHERS`, `FILE_PRIVATE`, `FILE_SINGLE_LINK`, `FILE_EXECUTABLE`, `FILE_TRUSTED_DIRECTORY` | The requirement bits of `MAELYS_CLI_FILE_*`; a secret takes `FILE_NO_SYMLINK \| FILE_OWNER_CALLER \| FILE_PRIVATE \| FILE_SINGLE_LINK`. `FILE_TRUSTED_DIRECTORY` judges the directory holding the file once symbolic links are resolved — owned by root or the caller, closed to group and world, and still holding that very object — which is what lets a link be followed as safely as refused; an installed manifest takes `FILE_REGULAR \| FILE_OWNER_TRUSTED \| FILE_NOT_WRITABLE_BY_OTHERS \| FILE_TRUSTED_DIRECTORY`. |
| `open_trusted(path, requirements)` | One open (`O_CLOEXEC`, `O_NONBLOCK`, `O_NOFOLLOW` under `FILE_NO_SYMLINK`), the requirements applied to `fstat` of the descriptor, then `FILE_TRUSTED_DIRECTORY` to the directory the path resolves to; a FIFO refused as not regular without blocking; returns a blocking descriptor at offset 0. |
| `read_trusted_file(path, requirements, minimum_size, maximum_size)` | `open_trusted` then a read bounded by the bytes actually read (`EFBIG` outside the bounds, whatever `st_size` said); returns a `bytearray`, zeroed before release on any failure after the open. |
| `read_regular_file(path, minimum_size, maximum_size)` | The same read without requirement: links followed, regular file required. |
| `check_file(path, requirements)` | Judges the path by `lstat`/`stat` without opening it, for a file that is not read (an executable). |
| `write_file_atomic(path, data, mode, policy)` | Private temporary in the destination directory, `fchmod`, `fsync`, then `WRITE_REPLACE` renames over the target or `WRITE_NO_REPLACE` links it and fails with `EEXIST` when any entry, a dangling link included, occupies the path. |
| `zero(buffer)` | Overwrites a `bytearray` with zeros; call it on a secret before dropping it. Python cannot zero an immutable `bytes`, which is why the readers return `bytearray`. |
| `FileError` | The `OSError` these functions raise; `.errno` is the C value (`EFTYPE` is `EINVAL` where the platform lacks it) and `.explanation` the short stable text of `out_error`. |
| `file_error_code(error_number)` | The stable code for an `errno`: `NOT_FOUND` (`ENOENT`, `ENOTDIR`), `ACCESS_DENIED` (`EACCES`, `EPERM`), `VALIDATION_FAILED` (`EFBIG`, `ELOOP`, `EMLINK`, `EINVAL`, `EISDIR`, `EFTYPE`), `IO_FAILED` otherwise; the table of `maelys_cli_file_error_code()`. |
| `file_failure(error, what=None)` | The `Failure` for an `OSError`, as `maelys_cli_fail_file()`: code from the table, message `what: strerror`, hint from the explanation. An `OSError` a handler lets escape is reported the same way. |
| `environment_format()` | The format `MAELYS_CLI_FORMAT` selects: `json`, `text`, or `text` for any other value. |

## Regular expressions

The counterpart of `maelys/cli/regex.h`: the reader of every
`argument.pattern`, open to a product for any other pattern written in the
subset of agent-cli/v2 section 3, with C's answers on the corpus
`c/tests/patterns.tsv`, refusals included.

| Name | Contract |
| --- | --- |
| `Regex(pattern)` | Compiles a pattern given as a `str`; `TypeError` otherwise, `RegexError` outside the subset. Never changed once built. |
| `Regex.search(value)` | Whether some part of the value matches, `^` and `$` holding at its two ends. A `str` is a sequence of code points, a lone surrogate (what `surrogateescape` makes of a byte that is not UTF-8) matched only by `.` and a negated class; `bytes`, `bytearray` or `memoryview` are read as UTF-8 as they are searched, never copied whole, a byte of no valid sequence being the character U+DC00 + the byte, so that a value and its `surrogateescape` decoding give the same answer. `.` matches all but U+000A, U+000D, U+2028 and U+2029. A Thompson simulation, never `re`: the work is O((len + 1) x states), with no backtracking. |
| `Regex.states` | The size of the compiled program, C's `maelys_cli_regex_states()`. |
| `RegexError` | The `ValueError` a refusal raises: `.code` the family (`empty`, `encoding`, `escape`, `bracket`, `group`, `quantifier`, `bound`, `limit`), `.status` its value in C, `.offset` the byte of the pattern, in UTF-8, where reading stopped, `.message` a sentence for a person whose wording may change. |
| `REGEX_MAX_BOUND`, `REGEX_MAX_DEPTH`, `REGEX_MAX_STATES` | 255, 32 and 65536, as in C. |

`MemoryError` propagates as anywhere in Python: it is C's `NO_MEMORY`.
C's `maelys_cli_regex_scratch_size()` and
`maelys_cli_regex_search_scratch()` have no counterpart: they exist so
that a C search need not allocate, and a Python search allocates as every
Python object does, with `MemoryError` as its only failure of resources.

## Proving it

`python/tests/test_maelys_cli.py` exercises the contract from the inside
through the reference product; `make conformance-check` runs the kit of
agent-cli-spec, at the commit `dependencies/agent-cli-spec.pin` names, against
`python/examples/hello.py` as it does against the C binaries;
`scripts/python-doc-check.sh` fails while a public name of the module is
missing from this page. CI runs the module tests on Python 3.9, the oldest
interpreter it declares, next to the current one. A product built on the
module runs the same kit on its own program in its CI.

The C library is the reference: where the two frameworks would differ, the
Python module is the one that is wrong, and the difference is a defect.
