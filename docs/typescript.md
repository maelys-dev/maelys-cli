# TypeScript and JavaScript runtime

The native Node implementation of agent-cli/v2 lives in `typescript/`.
It uses Node's standard library, has no runtime dependency, and neither
loads the C library nor starts Python. The supported host baseline is
Node 22.13.0 on Linux and macOS. One package serves both JavaScript and
TypeScript: ESM JavaScript in `dist/` with matching `.d.ts` declarations.

This is an unreleased implementation. The existing v0.13.0 tag predates it.
The package version follows `VERSION` during development; the next release
cut synchronizes it and its lock with the new framework version. The
native release workflow does not yet publish an npm channel. A local
archive must not be presented as a published release or attached to the old tag.

## Declaring a CLI

```typescript
import { Program, read, operand } from '@maelys-dev/cli';
const program = new Program('sample', 'Sample', '1.0.0', [
  read('show', 'show', 'Show one value.',
    invocation => ({ data: { value: invocation.operands[0] } }),
    { operands: [operand('VALUE', 'The value to show.')] })
]);
process.exitCode = await program.main();
```

`Program` validates one catalog before a handler runs. `ProgramOptions`
declares the guide, default command, canonical language, translation
tables and product text renderers. `main(argv)` returns a promise of the
exit code; use `process.exitCode` so buffered output drains.
`parse(argv)` validates without running a handler. `commandById`,
`resolve`, `family` and `complete` read that catalog.

The built-ins are help, version, describe, completion and __complete;
all trunk options are declared once. JSON success envelopes go to stdout,
failure envelopes to stderr, with exit codes 0, 1 and 2. Transactions
plan by default; --apply is the explicit write request. Legacy --dry-run
and --plan spellings are refused on transactions.

A line is read as in C and Python. The command is the line's first words,
before any option: `PROGRAM --json version` names none, and an empty line
is the help unless a default command is declared. Global options only, then
`--help` or `--version`, give the program's help or version
(agent-cli/v2 2.22.0, section 6), and `__complete` offers `--version` at
the root after global options or none. After a command that also begins
longer patterns (`catalog` and `catalog summaries`), it offers the operand's
choices, then the next words of those patterns, each once.

## Public declaration vocabulary

| Export | Purpose |
| --- | --- |
| `Program`, `ProgramOptions` | Program identity, validated catalog and invocation |
| `failure` | Canonical scalar C-style message template with separate human translation |
| `Failure` | Stable failure code, causal message, hint and optional details |
| `Invocation` | Validated operands, options, rendering and transaction state |
| `read` | Read or validation handler |
| `records` | Read whose reply contains count and records; supports JSONL |
| `transaction` | Preview/apply or preview/commit phases; optional plan binding |
| `execute` | Explicit action |
| `stream` | Protocol stdout owned by the handler |
| `external` | Delegate whose remaining words pass through verbatim |
| `option`, `flag` | Typed option or boolean flag, defaults and relationships |
| `operand` | Required, optional or final variadic operand |
| `argument` | Value kind, choices, bounds, width, algorithms or pattern |
| `constraint` | Requires, at-most-one or exactly-one rule |
| `example` | Invocation validated at catalog construction, never executed there |
| `Command`, `CommandOptions` | Command descriptor and declaration options |
| `Option`, `Operand`, `Value`, `ValueKind` | Typed input declarations |
| `Handler`, `Result` | Handler receives Invocation and returns data/exitCode, synchronously or asynchronously |
| `Json`, `Schema` | Reply values and JSON Schema declarations |

A handler returns `{ data, exitCode?: 0 | 2 }`. A stream/delegate handler may
return its process exit code. Failures throw `Failure(code, message, hint,
issues?, extensions?)`; issues carry code/message/path, extension keys
start with x-. Invalid failure metadata is answered as UNEXPECTED. Every
refusal of the line carries one issue, a reserved code and the path at
fault, as in C and Python (agent-cli/v2 2.23.0, section 8).

`failure(code, template, args, options)` keeps a canonical message template
for human translation; JSON always carries the canonical formatted message.
It accepts scalar C conversions (`%s`, `%d`, `%u`, `%x`, `%X`, `%o`, length
modifiers and `%%`), without width or precision. Argument mismatches and
unsupported conversions are refused rather than silently misformatted.

`Invocation.option('--name', fallback)` supplies the typed value or its
declared default. `flag('--name')` reads a flag, and `apply` reads the write
phase. `expect(fingerprint)` checks a plan binding before the handler writes;
the handler returns that same fingerprint. Rendering state is available as
`format`, `compact`, `nonInteractive`, `field`, `colorStdout` and
`colorStderr`. `detail`, `showProgress`, `progressDone` and
`progressWanted` implement diagnostics, silent in JSON.

## Values, patterns and output

`parseValue` applies the same value grammars and 64-bit ranges as C.
Values within JavaScript's exact integer range are numbers; larger integers
are bigint. Bounds outside that range must also be bigint. `stringify`
serializes them as exact JSON numbers, never rounded or converted to strings.

`Regex` compiles the pinned common ECMA-262/POSIX ERE subset to a Thompson
machine; `RegexError` names the refusal family and UTF-8 byte offset.
Matching is a search, anchors are strict, the four ECMA line terminators
are excluded by dot, and each invalid UTF-8 byte is one character.
The limits are 32 nested groups and 65,536 states. Matching uses bounded
state memory instead of V8's backtracking engine.

`recordText` emits escaped, tab-separated records with code-point sorted
columns; `fieldText` handles scalars, arrays and objects. `terminalColor`
applies the explicit option before environment defaults. Product text
renderers remain product-owned.

## Files and plans

`readRegularFile(path, minimum, maximum)` judges the opened regular file,
refuses a final symlink and enforces both bounds while reading.
`writeFileAtomic(path, data, mode, policy)` requires replace or no-replace,
flushes the file, then renames or atomically links it. It never silently
overwrites under no-replace. `fileFailure` maps Node errno to stable codes.

`Fingerprint` uses `add(label, value)` and `addFile(label, path, maximum)` frame
entries exactly like C/Python, distinguishing absent from empty values.
`finish()` returns the SHA-256 fingerprint.

These file primitives require POSIX; Windows is explicitly unsupported
for them. The initial Node surface does not expose counterparts of the
native trusted-process helpers, extension-manifest dispatcher or every
file-trust flag. It does not claim those guarantees. Delegation hands
the remaining words verbatim to a product handler; the product owns the execution.

## Build and verify

```sh
npm ci --prefix typescript
make typescript-check
make check-all
```

Dependency checkouts still come from the pinned release socle.
`make check` retains native/Python checks without requiring npm; the
separate Node CI job is required by the aggregate ci context. It runs
on Linux x86_64, Linux arm64 and macOS arm64.

Node tests consume the entire shared pattern corpus, including malformed
patterns and invalid UTF-8. The pinned spec kit runs on the example, the
full declaration surface and a default-command surface. Parity compares
C/Python/Node on common declarations and absent/existing-file fingerprints.
Existing C and Python samples deliberately differ in greet/list/check;
the comparison does not rewrite them to pretend they are identical products.

Built-in data, dynamic completion templates and shared declaration fixtures
are calculated from the C reference by:

```sh
python3 scripts/typescript-builtins.py --write
```

The check refuses stale generated data. The standalone package test packs
the module, installs it offline into a separate temporary project, and
executes a consumer without a source-tree/C/Python dependency.
