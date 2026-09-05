---
name: maelys-cli-framework
description: Add or change a feature of libmaelys_cli itself (descriptor field, value kind, macro, parser rule, describe member, built-in command, mechanic) while keeping the agent-cli/v2 contract, the catalog validation, the tests and every agent-facing document in step. Use for changes under include/, src/, cmd/, share/agents or docs of the maelys-cli repository; not for product CLIs built on the framework.
---

# maelys-cli framework change

Read `AGENTS.md` (constitution, contracts, procedure) before editing.

## Before code

1. Name the change class: mechanic, catalog, runtime or contract. The
   contract is `agent-cli/v2` as maelys-dev/agent-cli-spec writes it
   (`adapter/AGENT_CLI_SPEC_PIN`); `make conformance-check` runs its kit on
   `maelys-hello` and `maelys` and must stay green. A contract change goes
   to the specification first (pull request, tag, pin bump here), and must
   be additive within `agent-cli/v2`; otherwise the specification moves to
   `agent-cli/v3` and the contract constants follow.
2. Write down: the header declaration, the catalog validation rule, the
   parser rule and its causal position, the `describe` member, the
   completion effect, the accessor a handler will use, and the Python
   counterpart in `python/maelys_cli.py` (C is the reference; the Python
   module mirrors it or the changelog says why not).
3. Check the consumers' expectations: Maelys Git and Egress pin tags and
   commit generated references; anything that changes `describe` output
   (even additively) changes their generated files.

## Implement

- Header first, with a contract comment; new struct fields at the end,
  zero neutral; extend `MAELYS_CLI_*` macros, never their argument lists.
- Validate in `maelys_cli_catalog_validate()` with a message naming the
  command and the option or operand.
- Enforce in `maelys_cli_parse()` with a message naming the expected shape;
  share the value validator between options and operands.
- Expose in `describe` additively; keep `describe COMMAND_ID` minimal;
  update `__complete` candidates when relevant.
- Provide the handler-side accessor on the context; no private globals.
- Keep C11 strict (`-Wall -Wextra -Wpedantic -Wconversion -Werror`), no
  dependency, same behavior on Linux and macOS.
- Port the change to `python/maelys_cli.py` (standard library only, Python
  3.9, run with `-B`, never track `__pycache__`): same vocabulary, same
  messages where the kit reads them, same errno table and file primitives.

## Verify

1. Unit tests: accepted and refused inputs, `describe` fragments, catalog
   validation refusals, typed accessors; the same in
   `python/tests/test_maelys_cli.py` (`make python-check`).
2. `tests/test_cli.sh` for behavior visible from `maelys-hello` or `maelys`.
3. `docs/api-reference.md` for every public function
   (`scripts/api-doc-check.sh`); `docs/python.md` for every public name of
   the Python module (`scripts/python-doc-check.sh`); `share/agents/maelys-cli-guide.md` for
   every macro and accessor (`scripts/agent-doc-check.sh`); the block,
   skill, templates, `docs/command-conventions.md`, `docs/agent-cli.md`
   and `README.md` where user-visible. Add the feature's keyword per
   document in `docs/topics.tsv` (`scripts/doc-topics-check.sh`), which
   turns "did I document it everywhere" into a failing check.
4. `CHANGELOG.md`, `VERSION`, `include/maelys/cli/version.h`, then
   `make generate-cli-reference`.
5. `make check`, `make asan-ubsan`, `make analyze`, `make install-check`,
   `make cmake-check`. Push, wait for CI (Linux amd64/arm64, macOS, GCC),
   then tag; never move a pushed tag.
