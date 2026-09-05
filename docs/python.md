# The Python framework: `python/maelys_cli.py`

`python/maelys_cli.py` is the Python counterpart of `libmaelys_cli`: one
file, standard library only, Python 3.9 or later, implementing the
[agent-cli/v2](agent-cli.md) contract for a product written in Python. A
product pins maelys-cli through `adapter/MAELYS_CLI_PIN` as a C product
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

A command this build cannot run declares `unavailable="reason"`: it is
listed by `describe` with `available: false`, refused with `UNSUPPORTED`,
and never completed. `hidden=True` keeps it out of `help` and the completion.

## Operands, options and value kinds

`cli.operand(name, summary, required=True, variadic=False, kind=None,
choices=None, minimum=None, maximum=None)`; at most one operand is
variadic, and it is the last one. `cli.option(long, summary, argument=None,
default=None, required=False, repeatable=False, requires=(),
conflicts_with=(), group=None)`; `cli.flag(long, summary, ...)` is an
option without argument, and `--flag=false` is accepted. `cli.argument(name,
kind, choices, minimum, maximum, algorithms, pattern)` declares the value.

The kinds are those of the contract, validated before the handler runs and
returned typed by `invocation.option()` and `invocation.operands`:
`boolean`, `string` (optionally `pattern`), `integer` and `unsigned`
(ranges), `size` (`4K`, `16M`, `2G`, `1T`, in bytes), `duration` (unit
required: `ms`, `s`, `m`, `h`, `d`; in milliseconds), `path`,
`absolute-path`, `choice`, `hex` (`minimum`/`maximum` bound its length),
`sha256`, `digest` (`ALGORITHM:HEX` with `algorithms`). A `default` is
text, parsed with the same kind when the option is absent: a handler never
repeats a default. A repeatable option yields a list.

Errors are reported in the contract's causal order: unknown command;
option spelling, support and duplication; value kind, range and choice;
`requires`, `conflicts_with` and all-or-none `group`s; required options;
operand arity and kinds; rendering constraints. Inside the handler, raise
`cli.Failure(code, message, hint)` with one of the eleven stable codes.

## Rendering

`--format json` renders the envelope on stdout; a failure is an envelope on
stderr and exit 1, or `PROGRAM: [CODE] message` plus `Hint:` in text mode.
`MAELYS_CLI_FORMAT=json` in the environment selects JSON by default, which is
how an agent obtains the failure envelope of a `stream` command. Text
rendering of success is the `text` mapping given to `Program` (`{"greet":
render_greet}`), one line per record for `json-records`, or the data as
indented JSON.

## Proving it

`python/tests/test_maelys_cli.py` exercises the contract from the inside
through the reference product; `make conformance-check` runs the kit of
agent-cli-spec, at the commit `adapter/AGENT_CLI_SPEC_PIN` names, against
`python/examples/hello.py` as it does against the C binaries. A product
built on the module runs the same kit on its own program in its CI.
