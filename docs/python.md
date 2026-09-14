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

| Declaration | Effect | What the handler returns |
| --- | --- | --- |
| `cli.read(id, pattern, purpose, handler, ...)` | `read` | `(data, EXIT_OK)` or `(report, EXIT_VIOLATIONS)` |
| `cli.records(...)` | `read`, `json-records` | `({"count": N, "records": [...]}, EXIT_OK)`; `--format jsonl` accepted |
| `cli.transaction(..., commit=False)` | `{"plan": "preview", "apply": "apply"}` | `data.mode` is `plan` without `--apply`, `apply` with it; `--dry-run` and `--plan` are refused |
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
and never completed. `hidden=True` keeps it out of `help` and the completion.

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
value.
A `pattern` on a `string` or `path` argument is enforced (spec 2.3): the
value must match it (`re.search`, as POSIX ERE searches), else
`VALIDATION_FAILED`; `Program` refuses a pattern that does not compile.
Write it in the common subset of ECMA-262 and POSIX ERE (no `(?:`, no
`\d`, no lookaround) so the C parser reads the same motif.

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
`constraints`; required options; operand arity and kinds; rendering
constraints. Inside the handler, raise
`cli.Failure(code, message, hint)` with one of the eleven stable codes.

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
refuses `--field` together with `--format json` (`VALIDATION_FAILED`, even
when `MAELYS_CLI_FORMAT=json` resolved it) and a name absent from `data`,
after the handler has run. `--field` also lifts the records-only
restriction on `jsonl`.

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
`hidden=`, `unavailable=`, `synopsis=`, `protocol=`;
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
| `FILE_REGULAR`, `FILE_NO_SYMLINK`, `FILE_OWNER_TRUSTED`, `FILE_OWNER_CALLER`, `FILE_NOT_WRITABLE_BY_OTHERS`, `FILE_PRIVATE`, `FILE_SINGLE_LINK`, `FILE_EXECUTABLE` | The requirement bits of `MAELYS_CLI_FILE_*`; a secret takes `FILE_NO_SYMLINK \| FILE_OWNER_CALLER \| FILE_PRIVATE \| FILE_SINGLE_LINK`. |
| `open_trusted(path, requirements)` | One open (`O_CLOEXEC`, `O_NONBLOCK`, `O_NOFOLLOW` under `FILE_NO_SYMLINK`), the requirements applied to `fstat` of the descriptor, a FIFO refused as not regular without blocking; returns a blocking descriptor at offset 0. |
| `read_trusted_file(path, requirements, minimum_size, maximum_size)` | `open_trusted` then a read bounded by the bytes actually read (`EFBIG` outside the bounds, whatever `st_size` said); returns a `bytearray`, zeroed before release on any failure after the open. |
| `read_regular_file(path, minimum_size, maximum_size)` | The same read without requirement: links followed, regular file required. |
| `check_file(path, requirements)` | Judges the path by `lstat`/`stat` without opening it, for a file that is not read (an executable). |
| `write_file_atomic(path, data, mode, policy)` | Private temporary in the destination directory, `fchmod`, `fsync`, then `WRITE_REPLACE` renames over the target or `WRITE_NO_REPLACE` links it and fails with `EEXIST` when any entry, a dangling link included, occupies the path. |
| `zero(buffer)` | Overwrites a `bytearray` with zeros; call it on a secret before dropping it. Python cannot zero an immutable `bytes`, which is why the readers return `bytearray`. |
| `FileError` | The `OSError` these functions raise; `.errno` is the C value (`EFTYPE` is `EINVAL` where the platform lacks it) and `.explanation` the short stable text of `out_error`. |
| `file_error_code(error_number)` | The stable code for an `errno`: `NOT_FOUND` (`ENOENT`, `ENOTDIR`), `ACCESS_DENIED` (`EACCES`, `EPERM`), `VALIDATION_FAILED` (`EFBIG`, `ELOOP`, `EMLINK`, `EINVAL`, `EISDIR`, `EFTYPE`), `IO_FAILED` otherwise; the table of `maelys_cli_file_error_code()`. |
| `file_failure(error, what=None)` | The `Failure` for an `OSError`, as `maelys_cli_fail_file()`: code from the table, message `what: strerror`, hint from the explanation. An `OSError` a handler lets escape is reported the same way. |
| `environment_format()` | The format `MAELYS_CLI_FORMAT` selects: `json`, `text`, or `text` for any other value. |

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
