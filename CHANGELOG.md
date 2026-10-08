# Changelog

## Unreleased

- **A product's `agent_guidance` follows the width of the help.** 0.6.0 said
  of the new help that "no line passes it", and had measured that on the two
  reference products, neither of which declares a guidance. The text was
  printed as written: one long sentence was one long line -- 533 columns for
  maelys-egress, which reported it -- and lines broken by hand for 80
  columns passed any other width, which is what `maelys help` itself did,
  at 81 and 98 columns. Each line of the guidance is now a paragraph,
  wrapped to the help's width at that line's own indentation; a blank line
  stays a blank line. A product writes a paragraph on one line and breaks
  none by hand; one that already did, as maelys-egress, changes nothing.
  The dispatcher's own text is rewritten so, and a test now measures `maelys
  help`, which the tests of 0.6.0 did not. The Python module has no such
  field.

- The two third-party actions this repository pins in its own CI jobs move
  to their latest release: `actions/checkout` v7.0.1 (from v5.1.0), the
  commit the socle's own workflows already run here, and
  `actions/setup-python` v7.0.0 (from v5.6.0), which this repository alone
  uses, for the Python 3.9 job. Each stays pinned by commit, the tag beside
  it. The three Maelys pins were already at their latest: agent-cli-spec
  v2.12.0, maelys-json v0.2.0, maelys-release v0.63.0.

## 0.6.0 - 2026-10-08

- **A command declares its examples, and they cannot rot.** Examples lived
  in READMEs, where they drift: one product's README names two commands the
  binary its users have installed does not know. agent-cli-spec 2.12.0 adds
  an optional `examples` member at this repository's request, on one
  condition that makes it worth having: an example MUST be an invocation
  the command accepts.
  - **declaration**: `MAELYS_CLI_EXAMPLES(array)` of
    `MAELYS_CLI_EXAMPLE(words, summary)` in C, `examples=[cli.example(words,
    summary)]` in Python. `words` is the command line without the program's
    name, starting with the pattern, separated by single spaces;
  - **checked, never run**: the catalog validation parses each example at
    startup as it would a command line and refuses the catalog when one
    does not parse -- an option the command has lost, a value that is not
    of its kind, a required option or operand missing, a rule between
    options broken -- and when one shows a hidden option or asks for
    `--help`. The parser reads no file and starts nothing, so an example
    with `--apply` is checked like any other. After a delegate's pattern
    the words are the other executable's and are not checked. The same
    twenty cases are held in C and in Python;
  - **shown**: `help COMMAND_ID` has an `EXAMPLES` section, each a line to
    copy with its sentence below; the general help does not, and stays a
    screen. `describe` and `describe COMMAND_ID` list them, `describe
    --summary` omits them as it omits the output schema.

  `maelys-hello`, `hello.py` and `maelys` declare theirs; `make
  hello-parity-check` compares the examples of the commands the two
  reference products share. No categories: the families by identifier
  prefix are the grouping.
- agent-cli-spec pinned at v2.12.0 (from v2.10.0; 2.11.0 is skipped, having
  put the examples in the summary). 2.12.0 also writes that `--help` after a
  command gives its help and runs nothing, `--apply` included, and that the
  envelope then says `"command": "help"`: both hold here, the second since
  the change above it in this list. Its kit reads each declared example
  against the catalog, without launching anything.

- maelys-release adopted at v0.63.0 (from v0.62.3): the three workflow pins
  and one managed file. Its Impact line asks a gesture of this product: a
  managed file moves, so a product that carries
  `scripts/checkout-dependencies.sh` re-adopts rather than moving its pin,
  or `check` exits 2 on that file. The script now skips a pin whose file
  says `on-request` and prints how long each clone took; this repository
  declares no such pin, so it clones what it cloned.

- **`COMMAND --help` answers in the envelope of `help`, in C as in Python.**
  Under `--format json`, a C program named the command asked about in the
  envelope's `command` (`"command": "greet"`), over `data` that is the
  help's -- `text` and `commands` -- and that the output schema of `greet`
  does not describe; the Python module answered `"command": "help"`. The
  specification, measuring both while writing its rule on `--help`, asked
  which is right: `command` is what tells a consumer how to read `data`,
  so it is `help`, and the command asked about is in `data.commands`.
  `COMMAND --help` and `help COMMAND_ID` now return the same bytes.

- **A transaction can bind its application to the plan that was reviewed.**
  `--apply` plans again and applies that plan: the re-validation says the
  state still allows the transaction, not that the action is the one the
  caller read. agent-cli-spec 2.9.0 reserves one spelling for the binding,
  and the framework now offers it, in C and in Python:
  - **declaration**: `MAELYS_CLI_EXPECT_OPTION` beside
    `MAELYS_CLI_APPLY_OPTION`, `cli.transaction(..., expect=True)` in Python:
    `--expect FINGERPRINT`, a sha256 digest that requires `--apply`. The
    catalog validation refuses another shape of `--expect` on a transaction
    and a schema that does not list `fingerprint` in its `required`; on a
    read the name stays free;
  - **the fingerprint**: `maelys_cli_fingerprint_init/add/add_string/
    add_file/finish` (`digest.h`) and `cli.Fingerprint`, a `sha256:HEX` over
    the entries the product adds: the action and the state of what it would
    touch. Each entry is its label, a presence byte and its value, the two
    strings preceded by their length, so two entries are never one
    concatenation and an absent value is not an empty one. The two
    implementations frame identically: `make hello-parity-check` compares
    the fingerprints of both reference products, and both test suites hold
    the same reference strings, computed apart from either;
  - **the check**: `maelys_cli_expect(context, fingerprint)` and
    `invocation.expect(fingerprint)`, called before the first write. When
    `--expect` names another plan, `PRECONDITION_FAILED` with the hint to
    plan again, and nothing written;
  - **a handler that declares the option and never asks is not believed**:
    answering when `--expect` was given without having called the check is
    `UNEXPECTED`, not a success a caller would take for a binding.

  `note write` of `maelys-hello` and of `hello.py` are the worked example:
  the fingerprint covers the path, the content, `--replace` and what is at
  the path now, so the same note over a file that changed since is refused.
  Nothing changes for a transaction that declares none of this.
  `maelys agents install` does not offer it yet.
- **`help` fits the terminal.** The general help aligned every description
  on the longest usage: lines of 155 columns for `maelys-hello`, the
  purposes pushed past column 60 with blank space before them. A review of
  the contract named it, and a product had started to work around it. The
  layout is rewritten in the C library and the Python module, alike:
  - **width**: the terminal's when stdout is one, between 60 and 100
    columns; 80 anywhere else, so that a pipe, a file and `data.text` do not
    depend on a window. No line passes it: `help`, `help COMMAND_ID` and
    `help FAMILY` of both reference products are at most 80 columns;
  - **beside or below**: a description stands beside a label that fits its
    column and below one that does not; a line breaks between words, a
    usage between its groups, never inside `[--option VALUE]`;
  - **columns, not bytes**: an accented letter is one column, a CJK
    character two, a combining mark none. A purpose in French no longer
    wraps a third early;
  - **the general help is short**: each command by its pattern and its
    purpose, the product's first, then the ones every program has. It no
    longer repeats every usage;
  - **what every program has in common is named, not repeated**: the global
    options by their names and the agent contract by its first rule, where
    spelled out they were 28 of the 50 lines of `maelys-hello help` and the
    same in every product. `PROGRAM help conventions` has both whole. The
    general help of `maelys-hello` is 33 lines, 13 of them its commands;
  - **a family has its help**: `PROGRAM help FAMILY` and `PROGRAM FAMILY
    --help` list the commands under an identifier, the namespace `describe
    --summary --prefix` selects, each with its usage and its purpose below.
    Words that name no command are still `INVALID_COMMAND` without `--help`.

  The Python module's `help COMMAND_ID` gains the sections of the C
  library's (`USAGE`, `EFFECT`, `OUTPUT`) and its general help the `AGENT
  CONTRACT` paragraph; `display_width()` and `help_width()` are public.
  **What a product can see**: the text of `help` changes in all its forms,
  and the summary of the `help` operand by one sentence; `describe` is
  otherwise untouched, and nothing is to declare. Not done: categories or
  examples declared in the catalog, which would be an addition to the
  contract.

- agent-cli-spec pinned at v2.10.0 (from v2.9.0): the pin, and nothing in
  the framework. 2.10.0 says what a delegate is, on the criterion this
  repository gave when asked: `external: true` when the catalog does not own
  what follows the pattern, the words, the help and the completion there
  being another executable's. A command that keeps its grammar and relays
  the stdio of a child it starts is a `stream` with `external: false`,
  whether it names the child or takes it as an operand, which is `run` of
  `maelys-hello` and `channel exec` of maelys-egress. One rule joins the
  schema: `external: true` implies the effect `execute` and no `protocol`,
  which `MAELYS_CLI_EXTERNAL` and `cli.external()` have always declared. Run
  at the tag on the five programs before the pin moved: no failure.

## 0.5.36 - 2026-10-07

agent-cli-spec 2.9.0 writes down three rules a review of the contract found
broken here by reading; each was run before anything was changed, in C and
in Python. Its kit judges none of them on this repository's commands, the
report says so, and its verdicts do not move at the pin: the tests named
below are what holds them.

- **`--field` on a command that can write is checked against the catalog,
  before the command runs** (2.9.0, section 5). Until now the handler ran
  and a name its data did not carry was refused afterwards: `note write F
  --content hi --apply --field nosuch` wrote the file and answered
  `VALIDATION_FAILED`, where a caller that reads that code concludes that
  nothing changed. A transaction, with or without `--apply`, and an
  `execute` now accept only a name listed in the top-level `required` of
  their output schema. **What a product can see**: a writing command whose
  schema requires nothing, or that declares none, no longer accepts any
  `--field`, and a member its schema leaves optional is refused even when
  the run would have carried it. A `read` still decides on its data.
- **A `json-records` command that failed had already answered, in `jsonl`,
  in C.** `maelys_cli_emit_record()` wrote each line as it came, so a
  command that failed after two records left two lines on stdout and an
  error on stderr, where section 7 says a failure leaves stdout empty. The
  lines are held and written by `maelys_cli_finish_records()`; a trusted
  record is still written verbatim. Python already waited. The header
  promised "written immediately": a consumer that read a long listing while
  it ran now reads it at its end, and output that must flow is a protocol
  stream.
- **`--field` against a format from the environment was refused after the
  command had run.** With `MAELYS_CLI_FORMAT=json`, `note write F --content
  hi --apply --field path` wrote the file and then answered
  `VALIDATION_FAILED`, in C and in Python. The refusal now comes where the
  parser makes it for an explicit `--format json`: before the handler.
- **`MAELYS_CLI_FORMAT` is the default format in C as in Python.** C applied
  it only when no rendering option at all was given, so `--compact` beside
  it answered text, and `--field` with it escaped the refusal above; Python
  let only `--format` and `--json` override it, which is what "default
  format" says. `maelys_cli_invocation_t` gains a private `format_requested`.
- `docs/agent-cli.md` said that `describe --summary` carries
  `globalOptions`, `output` and `invariants`. It does not, in either
  implementation, and the specification forbids it; the sentence was wrong.
- agent-cli-spec pinned at v2.9.0 (from v2.8.1). It also reserves `--expect
  FINGERPRINT` and a required `fingerprint`, to bind a plan to its
  application; optional, and nothing in the framework offers it yet.

One case this release leaves as it is: on a terminal, in text, a
`json-records` command shows each record's human line as it comes, so a
failure can follow lines already shown.

## 0.5.35 - 2026-10-05

- **A Python program's completion launches no process at a Tab.**
  `completion SHELL` of the Python module prints a script that carries the
  candidates of the catalog, the first half of what agent-cli-spec 2.7.0
  opened and maelys-cli#98 asked. Measured on this machine, same shell,
  same word list (`limits --level low --`, 18 candidates): 97 ms per Tab
  under bash 5 and 109 ms under bash 3.2 for the script that calls
  `__complete`, 4.9 ms and 4.0 ms for the one that carries its candidates.
  The C library is unchanged: its `__complete` answers in 8 ms.
  - The script is `__complete` written in bash, zsh and fish over one table
    of the catalog: commands, options not yet given and repeatable ones
    again, `--option=choice`, choices of options and of typed operands,
    digest prefixes, identifiers after `help` and `describe`; hidden and
    unavailable commands and hidden options are not offered; a stream offers
    no shared option; files when there is nothing. After a delegate's
    pattern it calls `PROGRAM __complete`, the catalog not holding those
    words. Its first line names the catalog's `version`.
  - Three implementations of one algorithm are proved, not read:
    `make completion-check` drives every word list of
    `scripts/hello_words.py` and of a new Python completion surface
    (`python/tests/completion_surface.py`: a delegate, a stream, a hidden
    and an unavailable command, variadic and typed operands, a digest, a
    repeatable and a hidden option) in each installed shell against
    `__complete`, and counts the launches of the program: none, and one per
    word list after a delegate. 672 word lists locally, in bash 3.2, bash 5
    and zsh sourced and autoloaded. **fish is not installed on the machine
    this was written on**: its script is driven for the first time by this
    repository's CI, where its first run found that fish 4 reads `?` in
    a pattern as itself: the script counted every option as an operand. The
    surface joins `make conformance-check`, where the kit counts no launch
    and finds the version.
  - Every word of the table matches `[A-Za-z0-9._:/+@%,=-]+`, so a row is
    inert inside single quotes in the three shells. A catalog holding
    anything else gets the script that calls `__complete`, whole.
  - `Program(..., static_completion=False)` declines it, for a catalog that
    depends on the machine it runs on; `program.completion_script(shell,
    static=None)` returns either text. No `completion install`: 2.7.0 makes
    it optional, the parity rule would ask it of the C library too, and a
    package installs its completion.
- The Python module no longer imports `shlex`, `subprocess` and `tempfile`
  with itself, but where a pager or an atomic write needs them: its import
  goes from 67 ms to 25 ms, and one launch of `hello.py` from 133 ms to
  88 ms (medians of 60 interleaved launches; the bare interpreter is 52 ms).
  Every command gains it, a completion that still calls the program first.

- agent-cli-spec pinned at v2.8.1 (from v2.8.0), for its kit. A script that
  carries its candidates is 5 to 7 KB where one that calls `__complete` is
  600 bytes, and the 2.8.0 kit, which compares what `completion SHELL`
  prints on a pseudo-terminal with what it prints into a pipe, failed on
  macOS for a text whose line break fell on the 4096th byte: the terminal
  layer there emits `\r\r\n` at that boundary and the kit translated
  `\r\n` only. Found on the first CI run of the scripts above, reproduced
  60 times out of 60 for one of them and none for the others, reported with
  the remedy; 2.8.1 turns output processing off on its pseudo-terminal and
  compares byte for byte. Nothing else changes: a program that passed 2.8.0
  passes 2.8.1. A Python product that takes the static completion pins
  2.8.1 or later, or its macOS check fails at the whim of its catalog.
- agent-cli-spec pinned at v2.8.0 (from v2.7.0): the pin, and nothing in the
  framework. 2.8.0 is the specification's answer to the five points this
  repository reported while fixing its completion (maelys-cli#98, #99), and
  0.5.34 already does what it now says: a hidden or unavailable command is
  offered neither as a word nor as an identifier after `help` and
  `describe`, which its kit checks; after a delegate's pattern the program
  adds no word of its own, where 2.7.0 compared option names and would have
  failed a program relaying a delegate built on the same trunk; whether
  options are offered before a `-` is typed is the implementation's choice.
  Its kit drives every bash it finds, `/bin/bash` included, and takes a
  program named by a relative path again, so `make conformance-check` drops
  the absolute paths 2.7.0 required. Run at the tag on the four programs
  before the pin moved: no failure.

## 0.5.34 - 2026-10-05

- **The completion scripts did not complete, and every product built on the
  framework ships them.** agent-cli-spec 2.7.0 makes the script a rendering
  of `__complete` and its kit drives each script in its shell; maelys-cli#98
  lists what it found here. Measured on 0.5.33, then corrected in the C
  library and in the Python module, which now print the same three texts:
  - **bash**: `local IFS=$'\n'` came before the slice
    `"${COMP_WORDS[@]:1:COMP_CWORD}"`, and bash 3.2 — `/bin/bash` on macOS —
    joins that slice into one word when IFS holds no space. `__complete`
    received `'help '` as a single word, answered nothing, and the script
    offered files, for every word list. The words are taken first.
  - **zsh, C**: not in the issue, found by running its kit. The script read
    `${words[@]:1:CURRENT-1}`, which zsh 5.9 refuses with `unrecognized
    modifier 'C'`: the function failed at every Tab and nothing was ever
    offered but files. It reads `"${(@)words[2,CURRENT]}"`.
  - **zsh, Python**: `("${(@f)$(...)}")` is one empty element for an empty
    answer, so `_files` was never reached; and the words were not cut at the
    cursor.
  - **zsh, both**: the file opens with `#compdef`, which is the mark of a
    file to autoload from `fpath`, and worked only when sourced — autoloaded,
    the first Tab defined the function and completed nothing. Its last lines
    serve both.
  - **fish, both**: `-f` silenced fish's file completion with nothing in its
    place, and the C script passed `$current` unquoted, which drops an empty
    current word and completes the word before it. The function now offers
    the paths itself (`__fish_complete_path`) when `__complete` returns none.
    **fish is not installed on the machine this was written on**: the text is
    the one the specification's own fixture runs on its Linux runners, and
    this repository's CI is where it is first driven.
- **`__complete` after a delegate's pattern returns the delegate's words in
  every format** (section 9 of 2.7.0). The C library forwarded to the
  delegate in text mode by lending it its standard output, and returned
  nothing in JSON; it now reads the delegate's answer and returns it as its
  own records, drops a word carrying a control byte, and asks the delegate
  for `--format text` whatever `MAELYS_CLI_FORMAT` says. A delegate that is
  not installed is no longer reported on stderr at every Tab. The Python
  module offered its own global options there; it holds no delegate
  executable and returns none.
- **`__complete` is one oracle, not two.** Comparing both implementations on
  the word lists the two reference products share, 15 of 39 differed. The
  Python `_complete` is rewritten on `builtin_complete` of `src/app.c` and
  returns the same words in the same order:
  - options are offered after `--`, no longer on an empty word, where they
    hid the command words and the choices;
  - the choices of an option's own argument (`--level ` returned the other
    options) and the `--option=choice` spelling are completed;
  - a stream command offers no rendering option; the order is the catalog's
    rather than alphabetical.

  And three corrections on the C side, the reference being wrong there:
  - **an operand declared without a kind completed to `true` and `false`**
    (`maelys-hello greet <Tab>`): its kind reads as the flag's. It returns
    nothing, so the shell offers files;
  - a word that starts two commands (`agents install`, `agents status`) was
    returned twice;
  - an unavailable command was offered as an identifier after `help` and
    `describe`, which the Python module and this repository's conventions
    exclude, and where section 6 says "never an unavailable command". The
    test that asserted the contrary since 0.5.0 is reversed.
- Three gates, so that none of this returns: `make completion-check` drives
  the scripts of both reference products in every installed shell and
  compares with `__complete` — every bash found, `/bin/bash` included, since
  a newer bash on the PATH hides 3.2 from the kit, and zsh both sourced and
  autoloaded; `make hello-parity-check` compares `__complete` over 52 word
  lists and the three scripts between C and Python; `dependencies/packages`
  declares `zsh` and `fish` for the Linux runners, so the kit and the check
  judge the three shells there instead of skipping two. Run against 0.5.33
  on macOS, `completion-check` reports 44 differences.
- agent-cli-spec pinned at v2.7.0 (from v2.6.0). The kit drives the scripts
  from a directory of its own, so `make conformance-check` hands it absolute
  paths. Not in this release: the static script and `completion install` of
  2.7.0, which are optional and come next.

- maelys-release adopted at v0.62.3 (from v0.62.2): the three workflow pins,
  nothing else; `[asks: nothing]`, `[writes: tap]`. This repository is one
  the line is about: both formulas are bottled on `macos-15` and `macos-26`,
  so at the next release the `test do` of `libmaelys-cli` and of `maelys`
  runs on a poured bottle, and a test that fails there keeps the formula out
  of the tap instead of shipping it. Both tests are a compile-and-run smoke
  and two commands of the dispatcher; neither reads anything a bottle
  relocates.
- maelys-release adopted at v0.62.2 (from v0.62.1): the two workflow pins,
  nothing else. Its own Impact line says `[asks: nothing]`, and this
  repository adopts anyway for the other half of the line, `[writes: cut,
  migrate, tap]`: those three commands now read back what they pushed
  instead of taking the exit status of a `git push` for a publication —
  `cut` never compared which commit the tag it signed actually names on the
  remote. That is the class of defect that costs a version when it happens
  during a release, and the socle's own conventions name a version marked
  `writes` as one of the two reasons to adopt between releases.

## 0.5.33 - 2026-10-02

- `maelys_cli_process_options_t.exec_by_path`: execute the verified object
  through its own pathname rather than the descriptor held open across the
  check. The default is unchanged and is the stronger guarantee — the object
  executed is the object checked, with no window — and this is the exec the
  module already used for a script, re-checking device and inode against the
  trusted directory immediately before it. It exists for the one program the
  descriptor exec cannot run: `execveat(fd, "", AT_EMPTY_PATH)` leaves
  `AT_EXECFN` as `/dev/fd/N`, and a multi-call binary that reads its applet
  from that name refuses rather than guessing. Reported by maelys-egress,
  whose `channel exec` runs a program the user names, and measured here in a
  container: on Ubuntu's uutils coreutils 0.10.0, `/bin/sleep` answers
  "Security violation: Requested utility `4`" and exits 1 under the default,
  and runs to completion under the option — `argv[0]` and `/proc/self/exe`
  are intact, `AT_EXECFN` is what differs, so no choice of `argv[0]` fixes
  it. Recent Ubuntu ships uutils as its coreutils, so any product running a
  system tool through this API meets it.
- `docs/extensions.md` says how to isolate a faulty extension. A manifest
  the dispatcher cannot trust or understand still stops it entirely, which
  leaves an operator with no command to run — not even `maelys --version` —
  and the way out was folklore: maelys-egress found `MAELYS_COMMANDS_PATH`
  pointed at an empty directory on its own. The document now names both
  ways, in order: move the offending file aside, which the diagnostic
  identifies, or replace the search list for the single command that must
  run. It also says what the second one costs — every working extension
  hidden with the broken one — and that it belongs on one command line and
  never in a profile.

## 0.5.32 - 2026-10-01

- The flag `maelys_cli_process_signal` reads and `maelys_cli_process_wait`
  writes is an atomic. It was a plain `int`, which made the very thing
  `process.h` promises -- signal from one thread while wait blocks in
  another -- a data race in the C11 sense: undefined whatever it does in
  practice, and reported by ThreadSanitizer. Found by maelys-egress on its
  own TSan job, with the trace of its `channel exec` coordinator and its
  `sigwait` thread, against 0.5.31; its pull request could not pass that
  gate, and it could not work around it without serialising the signalling
  it needs. The store is a release after the status it guards and the loads
  are acquires, so a thread that sees the flag sees the status. No lock, no
  pthread dependency, and the guarantee is unchanged: nothing is reaped
  before `release`, so no signal reaches a stranger.
- `make tsan-check`, in `make check`: the one test that really runs signal
  and wait in two threads, built with `-fsanitize=thread` in its own build
  directory, since TSan and the address sanitizers cannot share a binary.
  Skipped with a word where the compiler has no ThreadSanitizer. Verified
  both ways: it reports the race on the flag as it was, naming the same two
  functions maelys-egress named, and passes on the atomic. The behaviour of
  those two threads is tested without a sanitizer too, in `make test`.
- One broken extension no longer costs the dispatcher. A manifest that is
  sound but declares a command this machine cannot run — its executable gone
  or untrusted, another `cliApi`, a digest that does not match — declares
  that command unavailable instead of stopping everything: `describe`
  reports `available: false` with the reason, `commands list` carries
  `available`, `unavailableReason` and `unavailableCode`, completion never
  offers it, and every other command keeps working, the dispatcher's own
  built-ins included. Until here, `maelys --version` died on a stale third-
  party digest: maelys-egress measured it on maelys-oci 0.6.6, whose brew-
  installed binary no longer matches the digest its manifest declares, and
  had to point `MAELYS_COMMANDS_PATH` at an empty directory to run anything
  at all. The worry the old rule answered — an agent must not believe a
  command is absent when it is merely broken — is answered better by
  declaring the command and saying why.
- What still stops the dispatcher: a manifest nothing can trust, invalid
  JSON, an unknown schema, a missing or invalid `cliApi` member, an invalid
  or reserved command name, a relative executable, a command declared twice.
  None of those can build a catalog anyone should rely on, and a relative
  executable is a manifest that is wrong rather than a machine that cannot
  run it.
- `maelys_cli_command_t.unavailable_code`, and `unavailable_code=` in
  `python/maelys_cli.py`: the code an unavailable command answers.
  `UNSUPPORTED` says "absent from this build or version" and is wrong for a
  cause that is not absence, so a digest that does not match answers
  `ACCESS_DENIED`, an executable that is gone `NOT_FOUND`, and a `cliApi`
  this dispatcher does not provide keeps `UNSUPPORTED`. An agent tells an
  incompatibility from a refusal of trust by the code and never by reading a
  sentence. One of the eleven stable codes, refused at startup otherwise,
  and refused without the reason beside it. A rendering flag no longer
  preempts the refusal of an unavailable stream command: asking `--format
  json` of one now answers the envelope that says why.
- `maelys_cli_extension_t` carries `unavailable` and `unavailable_code`, and
  `maelys_cli_extension_load` returns 0 for the four causes above rather
  than -1 — the behaviour of a public function, changed deliberately: the
  dispatcher is its only consumer in the fleet, and `discover` reports what
  it found rather than refusing a directory over one entry.
- Found by the fuzzer on the first run of this change, in CI: a manifest
  declaring another `cliApi` came back unavailable while carrying the
  relative executable it declared, because the `cliApi` verdict preceded the
  path check. A relative executable is now refused before any cause that
  only makes a command unavailable, so every extension the loader returns
  carries an absolute path whether it can run or not; the input is in the
  corpus as `relative-executable-other-api`. The `executable` member is also
  held to the terminal-safety rule its neighbours already had: `commands
  list` prints it, and an unusable extension is listed now, so a path
  nothing resolved reaches a terminal.

## 0.5.31 - 2026-10-01

- A refused manifest names the file that was judged. With a link — what
  every package manager installs — the directory at fault is the one at the
  other end, and the path an operator was given looks irreproachable: the
  diagnostic now carries `, resolved to PATH` and says the file *resolves
  into* an untrusted directory rather than *is in* one. Measured on a cellar
  layout before and after: the first wording sent a reader to a directory
  that was correct.
- `maelys/cli/files.h` gains the requirement `MAELYS_CLI_FILE_TRUSTED_DIRECTORY`:
  the directory holding a file once symbolic links are resolved must be owned
  by root or the caller, closed to group and world, and still hold that very
  object, judged on the descriptor the directory was opened through.
  `MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS` says who may write a file's bytes
  and says nothing about who may put other bytes at its path; this says only
  root or the caller may, which is also what lets a link be followed as
  safely as refused. `python/maelys_cli.py` gains `FILE_TRUSTED_DIRECTORY`
  with the same rule, explanations and errno.
- The `maelys` dispatcher reads a manifest with that requirement instead of
  `MAELYS_CLI_FILE_NO_SYMLINK`, so a manifest is now trusted exactly as the
  executable it declares — canonicalized, then judged with its directory.
  Homebrew links everything it installs from its cellar into the prefix, so
  every brew-installed manifest was refused (`Manifest
  /opt/homebrew/share/maelys/commands/egress.json is untrusted: path is a
  symbolic link`) and the dispatcher would not start at all on a machine
  carrying one, which is also why `make install-check` could not pass there.
  Refusing the link was never what protected against a link being rewritten
  by whoever controls its directory — someone who can write that directory
  can replace a plain manifest just as easily — and the directory rule is
  strictly more than was asked before: a manifest whose own modes are safe
  but whose directory is group-writable used to be accepted and is now
  refused. `docs/extensions.md` states the rule, what it protects, and the
  two consequences (a package manager's prefix is trusted for its owner and
  not for root; the resolved parent is judged, not every ancestor, since
  `/opt/homebrew/Cellar` is group-writable and an ancestor walk would refuse
  Homebrew while adding little). `docs/migration.md` records what this asks
  of maelys-oci and maelys-egress, whose manifests are the ones that were
  refused: nothing, beyond a store directory closed to others and a `sha256`
  computed over the binary as installed.

- maelys-release adopted at v0.62.1 (from v0.60.0), with `adopt . --product
  maelys-cli --apply`: the workflow pins and the managed
  `scripts/checkout-dependency.sh`. 0.61.0 is the one version since that
  asks this repository a gesture, and it asks it as a product that pins: the
  script clones from a git bundle when `MAELYS_DEPENDENCY_BUNDLES` names
  one, so a runner that may not read a private pin can be fed by a machine
  that may. Nothing else changes here — both pins of this repository are
  public, so no carry job and no `carried_dependencies` line are added to
  `ci.yml`, which the socle leaves to the product. 0.61.0 also makes `check`
  note a product with no fuzzing it can see, the third of the three points
  this repository sent after 0.57.1 and the last one open; this repository
  reads `ok tests/fuzz/: the socle's fuzz job runs it`. 0.62.0 verifies a
  release tag's signature against a list of allowed signers the socle
  publishes, read at the commit the product pinned, and asks nothing of a
  product signing with the key the fleet already uses.
- `maelys_cli_process_start()`, `_signal()`, `_wait()` and `_release()`: a
  program started with descriptors it inherits, signalled while it runs and
  waited for separately. `run` had neither — it inherits 0, 1 and 2, closes
  everything else at `exec` and blocks until the end — so a product needing
  either had to write the `fork`/`exec` the framework exists to keep out of
  products. `options->inherit` maps a descriptor of the caller onto the
  number the program expects, and the mapping is applied as a whole: every
  source first moves above the highest target, then each target is installed
  by `dup2`, which clears the close-on-exec flag it would otherwise keep.
  That order is what makes `3 -> 4` beside `4 -> 3` work, one source reach
  two targets, and a target equal to its own source arrive open; it also
  moves the three descriptors the child still needs — the error pipe, the
  held executable and its directory — out of the way of a target that would
  otherwise land on one of them and start the program without its
  descriptor, silently. Refused before the `fork`: a target below 3, two
  entries naming one target, a source that is not open, more entries than
  `MAELYS_CLI_PROCESS_MAX_INHERIT`.
- The handle holds the program's process id reserved until `release`: `wait`
  learns the status through `waitid` with `WNOWAIT` and reaps nothing, so
  `signal` from another thread can never reach a stranger that inherited the
  number — the race a product cannot close from outside. `signal` after
  `wait` is `ESRCH` and sends nothing; a second `wait` reports what the
  first noted. `run` is now `start` + `wait` + `release`, one
  implementation, its behaviour unchanged. Asked by maelys-egress for
  `channel exec`, which hands a mediated-connection socket to a program on a
  declared descriptor and relays signals to it from its `sigwait` thread;
  the three ways a target could have started the program without its
  descriptor are theirs, read in this repository's code before any of this
  was written.
- `maelys_cli_environment_to_envp_inherited()`: the caller's own environment
  with an overlay applied over it, the overlay winning on a name already
  carried. `maelys_cli_environment_to_envp()` builds the overlay alone,
  which is what a product wants when it replaces an environment and not what
  it wants when it adds two variables to one.
- No Python counterpart: `python/maelys_cli.py` has never carried the
  process primitives — the pager is its only `subprocess` call — so this
  adds nothing to a parity it does not have. A product written in Python
  that needs this reaches for `subprocess` with `pass_fds`, which is the
  same mapping with the same hazards and none of the refusals.

## 0.5.30 - 2026-09-24

- maelys-release adopted at v0.54.0 (from v0.51.1); the three managed files
  change and nothing else. 0.54.0 renames the legs of `check-product.yml`
  (`check (linux)`, `check (linux-arm64)`, `check (macos)`) so a required
  context stops carrying an image's version; this repository's ruleset
  requires `ci` alone, so there was nothing to narrow before adopting nor
  to widen after. 0.53.0 fixes the socle leaving a product's working copy
  detached — what happened here twice, reported as such — by moving only
  what it put under its own root; it also materialises the socle itself
  under `$MAELYS_DEPENDENCIES_DIR/maelys-release` from the `uses:` line,
  which `scripts/checkout-dependencies.sh` now does, attempted and never
  required. 0.52.0 announces a coming rule a version early through `check`.
- `docs/architecture.md` leaves this repository with its history, by
  `maelys-release migrate`, for the product's private documentation: the one
  prose document of `docs/` that the public `README.md` does not link. The
  five others stay, as the socle's own note says, until maelys-cli has a
  destination its readers can open. Its row leaves `docs/topics.tsv`.
- `docs/api-reference.md` and `docs/python.md` open on a `VERIFIED by`
  marker naming the check that holds them (`api-doc-check`,
  `python-doc-check`): the "verified reference" of the documentation
  policy (maelys-platform#95), a document `make check` reads and a public
  library cannot move into a private repository its contributors cannot
  open. `maelys-platform docs` classes them `verified`; they stay, with no
  destination. The five other prose documents stay held by the public
  README until a site documents maelys-cli.
- maelys-release adopted at v0.56.0 (from v0.54.0): the workflow pins and
  the managed prose of `AGENTS.md`, `CLAUDE.md` and the release skill, which
  now open on the rules every repository shares and name the rename order
  with its missing word — narrow, adopt, *merge*, widen — that 0.55.0
  restored. 0.56.0 sorts `docs/` on maelys-platform's conditions: the two
  verified references are `ok` rather than noted, and the five prose pages
  the public README links are named as held. `cut` now refuses an entry
  that does not name a dependency whose pin moved since the last tag.
- maelys-release adopted at v0.57.1, through v0.57.0 (from v0.56.0):
  workflow pins and the managed prose. `adopt` now says whether anything
  since the pin asks this product a gesture — nothing did: the adoption is
  by choice, and the socle says so in its JSON (`current: true`), as this
  repository had asked. 0.57.0 also spells the changelog heading it asks for
  as the entries spell it, and reports the pre-0.54.0 leg names as aliases
  so a rename never narrows a protection; neither touches a ruleset that
  requires `ci` alone. The socle now documents its own command line through
  this framework's generator, and holds itself to `check .` in its CI.
  0.57.1 only corrects `protect`, which runs at the checkout and was never
  run here: it moves the two workflow pins and nothing else.
- Fuzzing, which this repository did not have: the shared CI's fuzz job had
  been skipped on every pull request, for want of a command to run. Four
  harnesses under `tests/fuzz/`: `maelys_cli_run` on the catalog of
  `tests/catalog_surface.c`, which declares every form, so parsing, help,
  `describe`, completion and `__complete` read words an agent could type;
  every parser of `values.h`, whose accepted values must lie within the
  bounds given; the core's JSON validator and formatter, and the writer,
  whose output must validate whatever string it was handed; and
  `maelys_cli_extension_load` on arbitrary manifests. `make fuzz-smoke`
  replays the committed corpus, with every truncation and single-byte
  mutation of each seed, as plain programs: part of `make check`, so of
  every leg and of `make asan-ubsan`. `make fuzz` runs libFuzzer itself,
  bounded by `FUZZ_TIME`, and is the CI's `fuzz_command`.
  `tests/catalog_surface.c` keeps its catalog at file scope and its `main`
  behind `CATALOG_SURFACE_NO_MAIN`, so the harness drives the same catalog
  the property test and the conformance kit judge.
- maelys-release adopted at v0.60.0 (from v0.57.1): workflow pins and the
  managed prose. Two versions since ask this repository a gesture. 0.58.0,
  as a public one: the managed blocks of `AGENTS.md` and `CLAUDE.md` stop
  naming the private documentation repository, and so does the rest of this
  repository's unreleased text — the pointer `migrate` had
  written into `AGENTS.md` for the moved architecture document, and the
  entry above. Entries of published versions are left as they were tagged.
  0.58.0 also refuses a changelog title present twice and makes `protect
  --apply` refuse from a withdrawn version, two of the three points this
  repository sent after 0.57.1; the third, that `migrate` wrote the private
  documentation repository into the `AGENTS.md` of a public product, is
  fixed in 0.59.2, which names this repository's pull request. 0.60.0
  removes the pre-0.54.0 leg aliases, three jobs fewer on every pull
  request; this ruleset requires `ci` alone and required no alias, which
  `protect .` confirms.
- A build directory remembers the command line it was made with and drops
  what it holds when that line changes. Each variant has its own directory,
  so the sanitizers were never at risk; a flag passed by hand inside one
  was: `make check CC=gcc` after a build with cc relinked the objects of cc
  without recompiling a single source, which is how a diagnostic only one
  compiler emits stays invisible until CI — the signed-`char` defect of
  0.5.28 was found twice by the x86 leg and never here. The comparison
  happens while the makefile is read, and removes the stale objects there,
  rather than through a stamp every rule depends on: the make of macOS is
  3.81, which compares modification times to the second, so an object
  written in the same second as the stamp reads as up to date and is kept.
  Nothing is written on `make clean` or under `make -n`. Asked by
  maelys-warden, which compares its own flags and had found the equivalent
  defect at home.

## 0.5.29 - 2026-09-14

- Python: a `hex` argument or operand states its width with `digits`, an
  integer or a pair such as `[40, 64]`, and `describe` emits it so — the
  shape the C reference emits for `MAELYS_CLI_HEX` and `MAELYS_CLI_HEX_OR`.
  It bounded the length with `minimum`/`maximum` instead, a shape the
  contract allows too, so the same `--digest` was described one way by
  `maelys-hello` and another by `hello.py`; no product of the fleet
  declares a hex value in Python, so only the fixture changes. A `hex`
  without `digits`, or with `minimum`/`maximum`, is refused when the
  `Program` is built, as the C catalog validation refuses a hex without a
  width; `digits` on any other kind is refused too. `make
  hello-parity-check` compares the value members the two hellos declare
  under the same name, so a divergence cannot return in silence.
- `describe` states a bound of an `unsigned`, `size` or `duration` value
  only when the declaration does. It wrote `"minimum": 0` for every such
  value, the floor of the kind itself, and `"maximum": 18446744073709551615`
  for a maximum of 0, which the catalog defines as unbounded: a sentinel
  leaked as a number above 2^53 that a JavaScript reader cannot hold
  exactly, where an absent member says what was meant. The parity check
  found it on its first run, on `--memory` and `--wall-time` of the hellos;
  in the fleet it changes the descriptor of `--grace-seconds` of maelys-oci
  and of `--max-total-blob-bytes`, `--approvals` and `--required-approvals`
  of maelys-git-core, each into a shape the contract allows equally. The
  parser is unchanged: a maximum of 0 still accepts every value. Python
  drops a declared `minimum=0` on those kinds for the same shape.

## 0.5.28 - 2026-09-14

- `maelys-cli-embed` writes a byte above 127 as `(char)N`. It wrote every
  byte as a bare integer into a `const char[]`, and 226 fits no signed
  `char` while -30 fits no unsigned one, so a UTF-8 byte in an embedded
  text or schema description failed `-Werror=conversion` on x86 Linux and
  passed on arm64 Linux and macOS, where `char` is unsigned. CI found it
  twice in three days on an em dash in the agent texts; the cast is an
  implementation-defined conversion on both signednesses, with no
  diagnostic. Applied after every `--define` substitution ran on the bare
  values, so a pattern matches the same stream as before. The embed test
  now embeds UTF-8 and compiles it under `-Wconversion -Werror` with both
  `-fsigned-char` and `-funsigned-char`, reading it back byte for byte. The
  installed agent texts stay ASCII by habit, no longer by necessity.
- agent-cli-spec pinned at v2.6.0 (from v2.5.0). 2.6.0 lets an operand
  declare `digits`, `algorithms` and `pattern`, which only an option's
  argument could carry: an operand describes its value exactly as an
  argument does. The framework already emitted `digits` and `algorithms`
  on a typed operand, through the argument writer, so a hex operand was
  described in a shape the 2.5.x schema refused — what maelys-git-core
  read against the 2.4.0 schema, and what the property test could not see
  because its one typed operand was a path. Conformant since 2.6.0 without
  a change; the fixture now declares a fixed-width hex, a two-width hex, a
  digest and a patterned string operand, so the property test judges them.
- `maelys_cli_operand_t.pattern`: an operand takes a pattern as an option
  does — on a string or path kind, compiled at startup, refused otherwise —
  and the parser enforces it through the same synthetic descriptor that
  types the operand, in the operand's own wording. `python/maelys_cli.py`:
  `cli.operand(..., algorithms=, pattern=)`, the same refusal at
  declaration, enforced by `parse_value` as for an argument.
- 2.5.1 makes normative what this repository already did, and asks one
  thing more of a framework: the kit run against a program declaring every
  form it offers, not only a document validated against the schema.
  `make conformance-check` now runs the kit on `tests/catalog_surface`
  beside the three products (352 passed at 2.6.0). Reported in #69.
- Recorded, not changed: a hex width is `digits` in C and a length
  `minimum`/`maximum` in Python, both allowed by the contract, so the two
  reference implementations describe the same declaration differently.

## 0.5.27 - 2026-09-14

- maelys-release v0.51.1 adopted (from v0.40.1). The pinned dependencies now
  live under one root, never beside this repository: `maelys-release.conf`
  declares `[dependencies] apart`, `Makefile`, `CMakeLists.txt` and the two
  check scripts derive `maelys-json` and `agent-cli-spec` from
  `MAELYS_DEPENDENCIES_DIR`, and a build with no root fails on its own
  message — naming `maelys-release dependencies . --apply`, which
  materialises the pins under `~/.cache/maelys-release/dependencies/maelys-cli`
  — instead of reading whatever sits beside the repository. The managed
  `scripts/checkout-dependencies.sh` (plural) writes every pin under a root
  and prints the variable; every CI job that builds runs it into
  `$GITHUB_ENV`, and the `dependency_checkout` input, ignored by the socle
  since 0.6.0, is gone from `ci.yml`. Workflow pins, the managed
  `AGENTS.md`/`CLAUDE.md` block and the installed skill follow; `docs/cli.md`
  and `docs/cli-contract.json` are unchanged. Two stale Makefile comments
  fixed on the way: the maelys-json pin was quoted as `v0.1.0`, and the
  property test was still said to be outside `check`.
- agent-cli-spec pinned at v2.5.0. `input.constraints` states the
  cross-option rules rather than repeating them, and an all-or-none entry
  carries no name: its options are the whole rule, the name stays on each
  option's `group`, and the kit now checks that a command's entries and its
  groups agree. `describe` drops the `group` member it put on those entries —
  the second of the two members `make describe-schema-check` had found, the
  one this repository could not decide alone. With both closed, that check
  is part of `make check`. The contract ships the exhaustive reference
  catalog this repository's property test had suggested, and the kit's
  report names what it cannot see: declarations a program never emits.
- A command states the rules its option fields cannot say:
  `MAELYS_CLI_CONSTRAINTS(array)` of `MAELYS_CLI_CONSTRAINT(kind, options)`
  entries, appended last to `maelys_cli_command_t` with zero as neutral.
  `MAELYS_CLI_CONSTRAINT_EXACTLY_ONE` has no option-level form, so this is
  its only site — five sources of policy, exactly one of them, zero refused
  as two are, which is maelys-warden's case and what `conflicts_with` could
  not express (ten half-declarations that still accept the empty choice).
  `_AT_MOST_ONE` and `_REQUIRES` (the first option requires every other)
  state over several options what the pairwise fields say. Validated at
  startup — known options of the command, no duplicate, at least two — and
  enforced by the parser in the causal slot of the dependencies; stated by
  `describe` after the derived entries. `MAELYS_CLI_CONSTRAINT_ALL_OR_NONE`
  is refused at startup: all-or-none is declared by `.group`, and a rule has
  one declaration so that `describe` and the parser cannot drift apart.
  Reported by maelys-warden.
- `python/maelys_cli.py`: `cli.constraint(kind, *options)` and the
  `constraints=` command keyword, the same kinds, refusals and causal slot.
  The Python `describe` already carried no name on its all-or-none entries;
  the C one did — a divergence between the two reference implementations
  that the property test, which judges the C output, is what surfaced.

## 0.5.26 - 2026-09-14

- Fix: an option declared with `MAELYS_CLI_HEX_OR` describes its two accepted
  lengths as `"digits": [40, 64]`, the array the contract already declares,
  instead of `"digits": 40` beside an `alternativeDigits` member that the
  `argument` definition — closed with `additionalProperties: false` — never
  allowed. The framework had invented a shape next to the conforming one, and
  the unit test pinned the invented one, so it checked the framework against
  itself rather than against the contract. `make describe-schema-check`, which
  validates against the pinned schema, is what told them apart; it now reports
  only `constraints[].group`, whose repair belongs to agent-cli-spec.
- `make describe-schema-check` validates what the framework can serialize,
  not only what the reference products declare. `tests/catalog_surface.c`
  uses every declaration macro of `catalog.h` and every descriptor field,
  and `scripts/describe-schema-check.py` validates its catalog-wide,
  `--summary` and per-command `describe` against `schemas/describe.json` of
  the pinned specification, through that specification's own validator. The
  check also refuses while a declaration macro is missing from the fixture,
  comments excluded, so a macro added later cannot escape by never being
  exercised. It reports two members the 2.4 contract does not allow —
  `argument.alternativeDigits` from `MAELYS_CLI_HEX_OR`, and
  `constraints[].group` from all-or-none groups — which no product of this
  repository declares and the conformance kit therefore never judged. It is
  not in `make check` until a specification change accommodates them; the
  shape of `describe` is never changed here first. Reported by maelys-git-core.
- CI gains a `ci` job that succeeds only when `check`, `packaging`, `python`
  and `gcc` all did, and `main`'s branch rule requires that one status
  instead of the nine it required before. Those nine are named after a
  runner or the socle's matrix (`packaging (macos-15)`, `check / check
  (ubuntu-26.04)`), so a socle bump or a runner rename would have left the
  rule waiting on a status that no longer reports, blocking the branch with
  nothing downstream to lift it. `AGENTS.md` states the rule and its one
  door: the repository-admin role may merge what the rule refuses, for an
  infrastructure failure, as a human act — an agent session never does.
- maelys-release v0.40.1 adopted (from v0.38.0): workflow pins only. The
  managed `AGENTS.md`/`CLAUDE.md` block and the installed socle skill are
  unchanged, and so are `docs/cli.md` and `docs/cli-contract.json`. The
  versions in between carry `cut`'s audit of its own write, which this
  product reported (0.40.0) and whose refusal it then narrowed: a carrier
  that still holds the old version stops the release, a carrier that holds
  neither is a note, because a file that has simply stopped naming a version
  reads identically to one a cut forgot (0.40.1). The audit finds this
  product's `VERSION` and `include/maelys/cli/version.h`, both already
  covered by the `[cut] after-version` declared here, so it passes silently.
- `maelys-release.conf` declares `[gate] none`: this repository publishes
  without an approval step on the `release` environment, and says so instead
  of leaving the socle to report the gap at every `preflight`. What guards a
  publication here is upstream of the environment — a signed annotated tag on
  `main`, which only the signing key can create, on a commit whose checks
  `maelys-release cut` verifies twice, once on the release commit and once on
  the merge commit it tags. The socle's conventions note that an approver who
  may approve their own deployment is a pause rather than a control; the
  declaration names what the environment actually holds. Arming a reviewer
  later is one `gh api` call and `reviewer` on that line.

## 0.5.25 - 2026-09-11

- maelys-json pinned at v0.2.0 (from v0.1.6), which **removes**
  `maelys_json_value_pointer` and `maelys_json_document_parse_file_bytes`.
  0.1.6 had added them on this product's integration report, and the same
  report then observed that neither maelys-cli nor maelys-git-core has a
  call for either: a manifest is a flat object, and files are read by the
  consumer's own bounded reader. Nothing here called them, so no code
  changes; `maelys_json_error_pointer`, which this product does use, is
  untouched. `MAELYS_JSON_ABI_VERSION` is 2 and the CMake package declares
  0.2.x incompatible with 0.1.x, so the version this repository asks for
  moves with it: `find_package(maelys-json 0.2)` in `CMakeLists.txt` and in
  the installed `maelys-cli-config.cmake`, `maelys-json >= 0.2` in
  `maelys-cli-extension.pc`, and `docs/abi.md` says 0.2.
- maelys-release v0.38.0 adopted (from v0.28.1): workflow pins, the managed
  `AGENTS.md`/`CLAUDE.md` block and the installed socle skill. No content of
  `docs/cli.md`/`docs/cli-contract.json` changed. The block carries three
  rules this repository did not have: the release ceremony is
  `maelys-release cut DIR X.Y.Z --apply` then `--tag --apply`, a failed
  release is replayed with `gh workflow run release.yml --ref vX.Y.Z -f
  tag=vX.Y.Z` (`--ref` names the tag, because the `release` environment only
  accepts tags `v*`), and the prose of this repository belongs in maelys-docs
  rather than in `docs/` here.
- `maelys-release.conf` declares `[cut] after-version sh tools/sync-version.sh`.
  This product's version is materialised twice, in `VERSION` and in the
  `MAELYS_CLI_VERSION*` macros of `include/maelys/cli/version.h`, and
  `make check-version` fails when they drift. `cut` writes `VERSION` and
  commits the bump alone, so without this the header would stay behind and
  the release pull request's own checks would fail. The new script rewrites
  the four macros from `VERSION`, and `--check` reports drift without
  writing.

## 0.5.24 - 2026-09-11

- The dispatcher's sources move from `cmd/maelys/` to `cli/`, the spelling
  maelys-egress, maelys-oci and maelys-warden already use for the single
  binary a product builds. `cmd/<name>/` is maelys-git-core's convention,
  where five distinct executables justify one directory each; this
  repository builds one. Sources only: no header, no artifact, no installed
  path and no public name changes, and `git` records the three files as
  renames.
- maelys-json pinned at v0.1.6 (from v0.1.3). Every version in between is
  additive: 0.1.4 adds `maelys_json_writer_object_begin_except` and
  `maelys_json_error_pointer`, 0.1.5 moves the fuzz harnesses, 0.1.6 adds
  `maelys_json_value_pointer` and `maelys_json_document_parse_file_bytes`,
  hardens the contract of `error_pointer` and escapes the pointer in
  `maelys-json-canon` — the last three from this product's integration
  report. None of the new functions has a use here: a manifest is a flat
  object, so a semantic error already names its member, and manifests are
  read through `maelys_cli_read_trusted_file()`, never `parse_file`. The
  README no longer names a maelys-json version of its own, which had
  drifted to `v0.1.0`: it points at `dependencies/maelys-json.pin`.
- A manifest that does not parse now names the failing value by its RFC 6901
  JSON Pointer next to the line and column it already gave (`Manifest /path
  is not valid JSON at /version: line 1, column 25 ...`), so an agent reading
  the `PROTOCOL_FAILED` envelope has a structural position instead of a
  lexical one to recount. The pointer is omitted at the document root, where
  it names nothing, and when a key on its path carries a terminal control:
  the pointer decodes manifest keys and the metadata check never ran for a
  document that did not parse, so it is held to the rule `version` and
  `summary` already follow. `maelys_json_writer_object_begin_except` has no
  use here: maelys-json is read-only in this product, in `src/extension.c`
  alone, and the envelopes are written by the core's own writer.

## 0.5.23 - 2026-09-11

- agent-cli-spec pinned at v2.4.0. `--field NAME`, a rendering option in
  `globalOptions`, renders one top-level member of a command's `data`
  instead of the whole result, by the section 7 pipe rules extended to
  every JSON shape: an array of objects is one row per object, any other
  array is one value per line, an object is one row of its members, any
  other scalar is its escaped value on one line; in `jsonl` mode an array
  is one compact value per line and anything else is exactly one line, so
  rendering never depends on data shape. `--field` conflicts with an
  explicit `--format json`/`--json` (a filtered envelope would not
  validate against `outputSchema`), caught by the parser when both are
  explicit and again at reply time for `MAELYS_CLI_FORMAT=json`; a field
  name absent from `data` is `VALIDATION_FAILED`, discovered only once the
  handler has run. `jsonl` is now accepted on any command when combined
  with `--field` (previously refused outside `json-records`). Refused, like
  `--pager`, by a `protocol-stream` command.
- `python/maelys_cli.py`: `Invocation.field`, `field_text()`, `field_jsonl()`,
  the same causal-order refusals. `Program.guide()` also fixed to spell a
  free-string global option's argument by its name instead of assuming
  every option with an argument declares `choices` (latent since
  `--field` is the first such option).

## 0.5.22 - 2026-09-11

- Licensing: the repository's own `.claude/skills/maelys-cli-framework/SKILL.md`
  now carries the CC-BY-4.0 notice, like the installed
  `share/agents/claude-skill.md` (flagged by maelys-platform's audit).
  `LICENSING.md` names both cases under one section.
- maelys-release v0.28.1 adopted (from v0.21.1): workflow pins only, plus
  `scripts/checkout-dependency.sh` support for a pinned dependency's own
  submodules (declared, never automatic) and CC-BY-4.0 attribution on the
  socle's own installed skill and `AGENTS.md`/`CLAUDE.md` block. No
  content of `docs/cli.md`/`docs/cli-contract.json` changed. Versions
  v0.22.1 through v0.27.0 in between were no-ops for this product
  (resilient `apt-get`, opt-in packaging targets, publish channels,
  fuzzing, declarations output) or already matched (v0.28.0's agent-text
  attribution, already implemented in 3f65948/PR #45).

## 0.5.21 - 2026-09-10

- maelys-release v0.21.1 adopted (from v0.15.1): `docs/cli-reference.md`
  is now `docs/cli.md`, one name for every product's generated reference;
  `docs/cli.reference` declares what the socle cannot guess (`[build]
  build/release/bin`, `[programs] maelys maelys-hello`). The socle now
  regenerates and compares `docs/cli.md`/`docs/cli-contract.json` itself,
  using this repository's own `tools/generate_cli_reference.py` at the
  commit it is checked out at (maelys-cli does not pin itself): `make
  generate-cli-reference` and `make contract-check` are gone, replaced by
  `maelys-release check .` locally and `check-product.yml` in CI. No
  content change to the reference itself.

- The four texts `maelys agents install` writes (the `AGENTS.md`/`CLAUDE.md`
  block, `docs/maelys-cli-guide.md`, `.claude/skills/maelys-cli-command/SKILL.md`)
  now stamp the commit next to the version, e.g. `maelys-cli 0.5.20
  (bd7f689)`: a tag alone does not identify content, and a consumer's own
  pin is already a tag-and-commit pair for that reason. `tools/maelys-cli-commit`
  answers `git rev-parse` in a checkout, the new `COMMIT` file's
  `export-subst` expansion (`.gitattributes`) in a source archive that has
  none (a GitHub tag tarball, which Homebrew's `--build-from-source`
  builds from), or `unknown`. Consumers regenerate with `maelys agents
  install DIR --apply` to pick up the new stamp; the marker text changed,
  not its meaning.

## 0.5.20 - 2026-09-07

- `python/maelys_cli.py`: fixes issue #39, `--color` reaches the C
  invocation but not the Python one. `Invocation.color`,
  `.color_stdout` and `.color_stderr` mirror
  `maelys_cli_terminal_detect()`'s per-stream resolution
  (`terminal_color()`); a handler reads them instead of re-scanning
  `sys.argv` or the environment. The runtime's own failure rendering uses
  the same resolution, honoring an explicit `--color never` even before a
  command resolves, never an unresolved `--color always` (the C prescan's
  asymmetry).

## 0.5.19 - 2026-09-06

- agent-cli-spec pinned at v2.3.1: the `--prefix` grammar is written with a
  plain group, as the common dialect requires; the parser still accepts a
  product pattern spelled with `(?:`.
- maelys-release v0.15.1 adopted: release assets through a protected draft,
  `workflow_dispatch` replays the complete flow for an existing tag, checks
  and publication on Ubuntu 26.04. The product's own CI jobs move to the
  same runners.

## 0.5.18 - 2026-09-06

- agent-cli-spec pinned at v2.3.0. The trunk options `--progress
  auto|always|never`, `--verbose` and `--pager auto|always|never` exist on
  every command and in `globalOptions`: diagnostics on stderr in text mode
  only (`maelys_cli_verbose()`, `maelys_cli_detail()`,
  `maelys_cli_progress_wanted()`, `maelys_cli_progress()`,
  `maelys_cli_progress_done()`), silent under `--format json` or `jsonl`;
  the text rendering goes through `PAGER` (POSIX quoting, no shell) or
  `less` with `LESS=FRX` when stdout is a terminal, never in a pipe, in
  JSON or under `--non-interactive`; `--pager` is a rendering option that a
  protocol stream refuses.
- `.pattern` is enforced by the parser as POSIX ERE (`VALIDATION_FAILED`),
  and a pattern that does not compile is refused at startup; the `(?:` group
  of the contract's own `--prefix` grammar is compiled as a plain group. Text records into a pipe are
  tab-separated rows (union of member names sorted by code point, strings
  unquoted and escaped, other values compact JSON); the human line is shown
  on a terminal only. `describe --summary` no longer carries
  `globalOptions`, `invariants` and `output`.
- `python/maelys_cli.py`: the same trunk options, `Invocation.verbose`,
  `progress`, `pager`, `progress_wanted`, `detail()`, `show_progress()`,
  `progress_done()`, `record_text()`, `pager_command()`, `page_text()`;
  `pattern` enforced with `re.search`; `Program` refuses an invalid
  pattern.
- Fix: an external program started by `maelys_cli_process_run()` or
  `maelys_cli_process_replace()` keeps the caller's working directory. Since
  0.5.16 the pathname exec (macOS, and Linux for a script) changed to the
  executable's directory first, so every dispatched `maelys` extension and
  every delegate ran in its own directory and relative operands broke. The
  anchored inode check stays; the exec uses the canonical absolute path.

## 0.5.17 - 2026-09-06

- Security: trusted executables now require an absolute shebang interpreter
  and refuse interpreters named `env`, closing hidden current-directory and
  `PATH` lookups behind an otherwise absolute `execve` call.
- Security: extension `version` and `summary` metadata must be terminal-safe
  single-line UTF-8, preventing ANSI, line and bidirectional controls from
  reaching `maelys help` or `maelys commands list`; Arabic and left-to-right
  or right-to-left marks are covered with the other bidirectional controls.
- These process and extension facilities are C-only and have no Python
  counterpart.

- Installed agent texts (`share/agents/`) use CC-BY-4.0 with attribution to
  David Bromberg. Managed blocks, guide and skill retain their notices when
  installed; Make and CMake distribute the license. Other templates stay CC0.

## 0.5.16 - 2026-09-05

- maelys-release v0.14.2 adopted: workflows only (`uses:` lines at 1749a35;
  the generated header no longer carries the literal `\n` of 0.14.0).
- Security: `maelys-cli-embed --define` now performs literal byte
  substitution with `od` and `awk`; replacement text cannot become a `sed`
  program or execute a command. Invalid definition names are refused, with
  an adversarial regression test.
- Security: trusted extension manifests are checked and read through one
  descriptor; `maelys agents install` resolves and writes every managed path
  relative to an open project directory and refuses symbolic-link parents.
  Manifest executable aliases are canonicalized before checking and hashing.
- Security: external execution holds the checked executable and its trusted
  parent open through `exec`; Linux executes binaries by descriptor. Other
  inherited descriptors are marked close-on-exec, preserving the error pipe
  so interpreter and `exec` failures reach the parent as `errno`.
- Text failures, warnings, confirmations and dispatcher startup errors escape
  terminal control bytes originating in arguments or metadata.
- `python/maelys_cli.py`: explicit flag values now accept only the C
  spellings (`true/false`, `yes/no`, `on/off`, `1/0`); malformed values are
  refused instead of enabling transactional `--apply`. Text diagnostics also
  escape terminal control bytes.

## 0.5.15 - 2026-09-05

- maelys-release v0.14.0 adopted: the declarations move from `adapter/` to
  `dependencies/` (`dependencies/agent-cli-spec.pin`,
  `dependencies/maelys-json.pin`, `dependencies/packages`), the layout the
  socle now requires; the Makefile, the documents and the generated files
  follow. maelys-json pinned at v0.1.3 (packaging and test corpus only; the
  library API is unchanged).

## 0.5.14 - 2026-09-05

- agent-cli-spec pinned at v2.2.0: a hidden option. `.hidden` on
  `maelys_cli_option_t` keeps the option out of the derived synopsis, the
  help and the completion while the parser accepts it and `describe` lists
  it with `hidden: true` (emitted only when true); the catalog validation
  refuses a hidden required option. `maelys-hello greet --trace` is the
  example.
- `python/maelys_cli.py`: `hidden=` on `option()` and `flag()`, the same
  behavior; `Program.warn(message)`, the counterpart of `maelys_cli_warn()`.

## 0.5.13 - 2026-09-05

- `python/maelys_cli.py`: `synopsis=` on every declaration function
  overrides the derived usage, as `.synopsis` does in C (must start with
  the pattern; the catalog and `describe` still carry every option). For a
  product whose trial options should not appear on the usage line.
- `python/maelys_cli.py`: its public API is declared stable in
  `docs/python.md` ("Stability of the module"), with the versioning of the
  C library: additive within `0.5`, a `0.6` otherwise, and every change to
  the module named in this changelog under a line starting with the module
  path, for the consumers that vendor the file at `adapter/MAELYS_CLI_PIN`.
  No code change.

## 0.5.12 - 2026-09-05

- `python/maelys_cli.py`: the framework for a product written in Python,
  one file, standard library only, Python 3.9 or later. The declaration
  vocabulary mirrors the C macros (`cli.read`, `cli.records`,
  `cli.transaction`, `cli.execute`, `cli.stream`, `cli.external`;
  `cli.operand`, `cli.option`, `cli.flag`, `cli.argument` with every value
  kind of the contract), with the built-ins, the envelopes, the exit codes,
  the causal order of refusals, `describe --summary --prefix`, the
  completion and `MAELYS_CLI_FORMAT`. `python/examples/hello.py` is its
  reference product, checked by `python/tests/test_maelys_cli.py` and by
  the conformance kit in `make check`. `docs/python.md` is the guide; the
  agent texts mention the module.
- The Python module follows the C library as its reference: `pattern` is
  informative, `MAELYS_CLI_FORMAT` accepts `json` and `text` only, an
  `OSError` maps to the stable code through the table of
  `maelys_cli_file_error_code()` (`file_error_code`, `file_failure`), and
  the file primitives of `files.h` exist with the same requirements,
  errno values and explanations (`open_trusted`, `read_trusted_file`,
  `read_regular_file`, `check_file`, `write_file_atomic`, `zero`,
  `FileError`). `AGENTS.md` and the framework skill require the port in
  every change; `scripts/python-doc-check.sh` holds `docs/python.md` to the
  module; CI runs the module tests on Python 3.9. `python-check` is part of
  `make check` only where `python3` exists, like `contract-check`, runs
  with `-B`, and `__pycache__` is no longer tracked.

## 0.5.11 - 2026-09-05

- `maelys_cli_open_trusted()` and `maelys_cli_read_trusted_file()` judge
  the descriptor they open (`fstat`), so the file checked is the file read;
  the open never blocks (a FIFO is refused as not regular), `O_NOFOLLOW`
  applies under `MAELYS_CLI_FILE_NO_SYMLINK`, and the read is bounded by
  the bytes actually read, never by the size observed before it.
  `maelys_cli_read_regular_file()` is now that read without requirement.
- Requirements `MAELYS_CLI_FILE_SINGLE_LINK` (`EMLINK`) and
  `MAELYS_CLI_FILE_OWNER_CALLER` (`EPERM`), for secrets.
- `maelys_cli_zero()` (zeroing the compiler cannot elide) and buffers zeroed
  before release on every read failure.
- `maelys_cli_file_error_code()` maps an errno to the stable code and
  `maelys_cli_fail_file()` reports it with the explanation as hint.
- Tests exercise every system-call failure of `files.c` through a fault
  hook compiled only into the test copy of the file.

## 0.5.10 - 2026-09-04

- agent-cli-spec pinned at v2.1.0: `describe --summary --prefix PREFIX`
  returns one command namespace with a `filter` member (`INVALID_COMMAND`
  when empty, `VALIDATION_FAILED` on a malformed prefix or a misuse), and an
  option may now conflict with an operand (`.conflicts_with = "COMMAND_ID"`,
  exposed in `conflictsWith` and `input.constraints`), and a string or path
  option may document its regular expression (`.pattern`, exposed as
  `argument.pattern`). Conformance: maelys-hello passes 110 checks and
  maelys 90 with the v2.1.0 kit.

## 0.5.9 - 2026-09-04

- `docs/command-conventions.md` and `docs/agent-cli.md` are reduced to what
  `libmaelys_cli` adds to `agent-cli/v2` (catalog declarations, causal
  parser order, rendering decisions, completion, proof of implementation);
  the contract itself is read in maelys-dev/agent-cli-spec.
- Release socle upgraded to maelys-release 0.6.1.

- The contract `agent-cli/v2` is now specified once, in
  maelys-dev/agent-cli-spec, pinned in `adapter/AGENT_CLI_SPEC_PIN`; its
  conformance kit runs on `maelys-hello` and the `maelys` dispatcher in
  `make check` (`conformance-check`), so the framework is held to the
  written contract like every other implementation. `AGENTS.md` and the
  `maelys-cli-framework` skill tell an agent that a contract change starts
  in the specification, never here. `docs/command-conventions.md`
  and `docs/agent-cli.md` point at the specification and keep what is
  specific to `libmaelys_cli`.

## 0.5.8 - 2026-09-04

- CI aligned on the socle: `ci.yml` calls maelys-release's reusable
  `check-product.yml` at the version `release.yml` pins, so CI and release
  share the same dependency checkouts, packages and checks; the product
  keeps its packaging and GCC jobs.
- Build: maelys-json is compiled inside this tree per build variant
  (`build/<variant>/maelys-json`) with the same flags, instead of the
  sibling's own build directory that a recursive `make` with `BUILD=`
  could not find.

## 0.5.7 - 2026-09-04

- `maelys-cli-reference --neutral-availability [IDS]` describes every
  command, or the identifiers given, as available whatever the host: a
  command `.unavailable` on some hosts (maelys-oci's Linux-only
  `unpack-rootfs`) made the committed contract host-dependent, and the
  product had to normalize it with a script of its own.
- Release socle upgraded to maelys-release 0.5.0: the managed
  `scripts/checkout-dependency.sh maelys-json` replaces the product's
  `scripts/checkout-json.sh`, and the CI drift step calls
  `maelys-release check`.

## 0.5.6 - 2026-09-03

Feedback from the Maelys OCI open-core split (additive, hence a patch
release: the CMake package is compatible within a minor and consumers
request `0.5`):

- `maelys_cli_catalog_concat()`, `maelys_cli_catalog_part_t` and
  `MAELYS_CLI_CATALOG_PART`: a catalog composed from parts at startup.
  A later part may provide a command that an earlier part declares
  `.unavailable`, replacing it in place so the extended build offers it at
  the same position in help and describe. Any other repeated identifier is
  refused with `EEXIST`: composition never shadows a real command silently.
  The `maelys` dispatcher composes its own catalog with it.
- Regenerate the release workflow with maelys-release 0.2.8 (the tap publish
  job no longer trips on a duplicate formula class).

## 0.5.5 - 2026-09-03

- The `maelys` formula description no longer starts with the formula name
  (`brew style` refused it, so 0.5.4 published no formula) and the release
  workflow is regenerated with maelys-release 0.2.6, which taps
  `maelys-dev/tap` before building bottles so `libmaelys-cli` finds
  `libmaelys-json`.

## 0.5.4 - 2026-09-03

- Regenerate the release workflow with maelys-release 0.2.5. The `v0.5.3`
  tag exists but produced no release: the publish job of the socle expected
  deb and rpm packages that this repository does not ship.

## 0.5.3 - 2026-09-03

- Release through the shared maelys-release workflows and publish two
  Homebrew formulas: `maelys`, the command alone (`make install-dispatcher`),
  and `libmaelys-cli`, the framework to build a product CLI (`make
  install-sdk`); `make install` still installs both. Both formulas build
  against the `libmaelys-json` formula. `scripts/package-release.sh TARGET`
  stages the installed tree; `adapter/MAELYS_JSON_PIN` and
  `scripts/checkout-json.sh` record and fetch the pinned maelys-json.

## 0.5.2 - 2026-09-03

Feedback from the Maelys Git 0.5 integration:

- `maelys-cli-reference` takes `--title`, `--intro`/`--intro-file`,
  `--columns` and `--global-label`, so a product keeps the language and
  header of its generated reference.
- CMake looks for maelys-json only when `MAELYS_CLI_BUILD_EXTENSION` is on;
  a core-only consumer (`-DMAELYS_CLI_BUILD_EXTENSION=OFF`) never configures
  the sibling project.
- `MAELYS_CLI_DEFAULT_OF(constant)` declares `default_text` from a numeric
  constant of the product library, giving a default a single source.
- `docs/topics.tsv` and `scripts/doc-topics-check.sh` (part of
  `make check`): a topic coverage contract listing, per document, the
  keywords it must mention, so documentation cannot lag behind a feature.

## 0.5.1 - 2026-09-02

- Documentation only: the installed agent texts, the product templates,
  `docs/agent-cli.md`, `docs/abi.md` and `SECURITY.md` describe the 0.5.0
  dependency boundary (dependency-free core, `libmaelys_cli_extension.a`
  on maelys-json, one archive copy per executable), the writer's UTF-8
  strictness and the completion changes.

## 0.5.0 - 2026-09-02

Self-review of the framework and dependency boundary:

- Manifest discovery moves to `libmaelys_cli_extension.a`, which reads
  manifests through maelys-json (bounded parsing, duplicate keys and
  invalid UTF-8 refused). The core `libmaelys_cli.a` stays dependency-free
  and no longer exposes a document reader: `maelys_cli_json_object_get`,
  `maelys_cli_json_decode_string` and `maelys_cli_json_decode_unsigned` are
  removed. pkg-config `maelys-cli-extension` and CMake
  `maelys::cli_extension` declare the dependency; archives never embed it.
- The core writer refuses invalid UTF-8 so envelopes are always valid JSON.
- `maelys_cli_process_run` closes inherited descriptors with
  `close_range` on Linux and `closefrom` on the BSDs.
- Completion: `help` and `describe` complete command identifiers,
  unavailable commands are never offered, and the `maelys` dispatcher
  forwards completion of an external command to that command's own
  `__complete`.

## 0.4.2 - 2026-09-02

- The installed agent guide lists every handler accessor;
  `scripts/agent-doc-check.sh` (part of `make check`) keeps the guide in
  step with the macros of `catalog.h` and the accessors of `app.h`.
- Framework change procedure for agents working on this repository:
  `AGENTS.md` and the `maelys-cli-framework` Claude skill.

## 0.4.1 - 2026-09-02

- Documentation only: the installed agent instructions (`AGENTS.md` /
  `CLAUDE.md` block, guide, Claude skill), the product templates and
  `docs/agent-cli.md` now cover every feature added in 0.2 to 0.4 (typed
  kinds and operands, dependency groups, typed defaults, unavailable
  commands, `maelys_cli_replied`, helper resolution, trusted emitters,
  completion, `MAELYS_CLI_FORMAT`, minimal `describe`). Run
  `maelys agents install DIR --apply` again in consumer projects.

## 0.4.0 - 2026-09-02

Feedback from the Maelys Git migration:

- Synopses are derived dynamically (`maelys_cli_command_synopsis_alloc`);
  the catalog validation measures every synopsis against
  `MAELYS_CLI_MAX_SYNOPSIS` (4096) and names the offending command. Required
  options now precede optional ones in the derived synopsis.
- Dependency groups: `.depends_on_all` (NULL-terminated list) and `.group`
  (all-or-none), enforced by the parser and exposed in `describe`
  (`requires` lists, `all-or-none` constraints, per-option `group`).
- Typed defaults: `default_text` is validated against the option's kind at
  startup and returned by `maelys_cli_option_unsigned/integer/choice` and
  `maelys_cli_option_or` when the option is absent; handlers no longer
  repeat defaults.
- Unavailable commands: `.unavailable = "reason"` replaces a failing
  handler; the command stays in `describe` (`available: false`,
  `unavailableReason`) and fails with `UNSUPPORTED`.
- `maelys_cli_succeed_trusted` and `maelys_cli_emit_record_trusted` write
  serializer-guaranteed JSON verbatim in compact and jsonl modes.
- CMake package: `add_subdirectory`, FetchContent or
  `find_package(maelys-cli)` provide `maelys::cli`, `MAELYS_CLI_EMBED` and
  `MAELYS_CLI_REFERENCE`; `make cmake-check` proves it.
- `maelys-cli-reference` (the reference generator) is installed with
  `maelys-cli-embed`.

## 0.3.0 - 2026-09-02

Feedback from the Egress migration:

- Shell completion generated from the catalog: `PROGRAM completion
  bash|zsh|fish` prints a shim that calls the hidden `__complete` command,
  which returns candidates for command words, options, `--option=value`
  choices, digest algorithm prefixes and typed operands.
- Typed operands: `maelys_cli_operand_t` carries a kind, choices and
  limits (`MAELYS_CLI_OPERAND_CHOICE`, `MAELYS_CLI_OPERAND_KIND`), validated
  by the parser, exposed in `describe` and readable through
  `maelys_cli_operand_choice()`, `maelys_cli_operand_unsigned()` and
  `maelys_cli_operand_integer()`.
- `describe COMMAND_ID` is minimal: `globalOptions`, `output` and
  `invariants` are only part of the inventory forms.
- `MAELYS_CLI_FORMAT=json|text` selects the default rendering; for
  protocol-stream commands it only shapes the failure envelope on stderr.
- `maelys_cli_json_string()` refuses `NULL` instead of writing `null`.
- The reference generator omits product and framework versions unless
  `--include-versions`, so `contract-check` is now part of `make check`.
- The public headers state that the `maelys_cli_` / `MAELYS_CLI_` namespace
  is reserved for the framework.

## 0.2.0 - 2026-09-02

Feedback from the first migrations (Maelys Git, Warden, Egress):

- New value kinds `absolute-path` (`MAELYS_CLI_ABSOLUTE_PATH`) and `digest`
  (`MAELYS_CLI_DIGEST`, `ALGORITHM:HEX` with the length implied by the
  algorithm), so handlers no longer re-validate strings.
- `maelys_cli_replied()` exposes the reply state for helpers that may reply;
  the guide documents the pattern.
- `maelys_cli_resolve_helper()` and `context->executable` expose the
  delegate search order and `argv[0]` to handlers.
- Error messages grow to 4096 bytes and hints to 1024
  (`MAELYS_CLI_MAX_ERROR_MESSAGE`, `MAELYS_CLI_MAX_ERROR_HINT`).
- Documented that `--dry-run`/`--plan` are refused only on commands that
  declare `--apply`.

## 0.1.0 - 2026-09-02

- Licensing: framework code under MPL-2.0; agent texts and product
  templates under CC0-1.0 so consumers can copy and edit them freely
  (`LICENSING.md`).
- Initial release of `libmaelys_cli`: product-neutral value parsing,
  environment overlays, bounded file I/O with atomic writes, SHA-256,
  dependency-free JSON writer/validator/formatter, terminal detection and
  safe process invocation extracted from the Warden CLI.
- Declarative command catalog with typed operands and options, causal
  validation, derived synopsis, `help`, `version` and machine-readable
  `describe` following the `agent-cli/v2` envelope of Maelys Git.
- Rendering modes `text`, `json` and `jsonl`, stable error codes and exit
  codes `0`, `1`, `2`, plan/apply transactions with `--apply`.
- External commands: delegate commands resolved beside the executable or in
  declared helper directories, executed with `execve` and no shell.
- `maelys` dispatcher discovering external commands through verified
  `maelys.cli-extension/v1` manifests, and `maelys agents install|status`
  to manage Claude Code and Codex instructions in consumer projects.
- Declaration macros (`MAELYS_CLI_READ`, `MAELYS_CLI_TRANSACTION`,
  `MAELYS_CLI_STRING`, `MAELYS_CLI_SIZE`, ...) over designated initializers,
  and `maelys-cli-embed` to embed JSON Schema files and other texts as C
  symbols, so catalogs never carry hand-escaped JSON.
- `MAELYS_CLI_PROTOCOL_STREAM` names the protocol owning a stream command's
  stdio (`protocol` in `describe`); `MAELYS_CLI_HEX_OR` accepts two
  hexadecimal lengths for object identifiers; the reference generator takes
  the binaries to aggregate as arguments.
- CI workflow for Linux amd64/arm64 (clang and GCC) and macOS; Egress
  migration path and the JSON writer decision documented. Verified with
  GCC 14 on Linux (glibc feature macros, `/proc/self/exe`) and clang on
  macOS; the option field is named `depends_on` because `requires` is a
  C++20 keyword (the `describe` member stays `requires`).
- `make contract-check` rejects a stale generated reference (locally and in
  CI); short product templates for `command-conventions.md` and
  `agent-cli.md` installed under `share/maelys-cli/templates/`.
- Reference product CLI `maelys-hello`, unit tests, end-to-end CLI tests,
  C++ header gate, sanitizer target and reference generator.
