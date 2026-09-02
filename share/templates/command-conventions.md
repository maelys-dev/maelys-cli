# @PRODUCT@ command conventions

<!-- Template installed by maelys-cli under share/maelys-cli/templates/
     (CC0-1.0, edit freely).
     Copy it to docs/command-conventions.md, replace @PRODUCT@ and @PROGRAM@,
     and keep only what is specific to this product. The framework rules are
     normative and live in the installed framework documentation; do not
     copy them here. -->

The `@PROGRAM@` command line is a public protocol built on `libmaelys_cli`.
The framework conventions apply in full: declarative catalog, `describe`,
`agent-cli/v2` envelopes, causal validation, plan/apply with `--apply`,
stable error codes and exit statuses `0`, `1` and `2`. They are documented
in `command-conventions.md` of the maelys-cli distribution
(`PREFIX/share/maelys-cli/docs/`).

## Grammar

```text
@PROGRAM@ COMMAND [SUBCOMMAND] [OPERANDS] [OPTIONS]
```

<!-- Product-specific grammar notes: configuration file versus flags,
     mandatory operands, conventions on paths. -->

## Output channels

```text
stdout       result data or the declared protocol stream
stderr       diagnostics only
exit status  0 completed, 1 failed, 2 validation report with violations
```

<!-- List every protocol-stream command and the protocol that owns its
     stdout, for example:

| Command | Protocol | Notes |
| --- | --- | --- |
| `serve` | `@PROGRAM@-lifecycle/1` JSON Lines | drain until exit |
-->

## Commands with a validation report

<!-- Commands that exit 2 when their report contains violations, and the
     `data` field that carries the verdict, for example `config validate`
     with `data.valid`. -->

## Commands not provided by every build

<!-- List commands declared with `.unavailable` in some builds (for example a
     Cloud agent behind a build option), with the reason `describe` reports. -->

## Shell completion

`@PROGRAM@ completion bash|zsh|fish` prints the completion script generated
from the catalog; packages install it in the shell's completion directory.

## Sources of truth

- the catalog in the product sources defines commands, operands and options;
- `schemas/*.json` define `data` of every command and are embedded at build
  time;
- `docs/cli-reference.md` and `docs/cli-contract.json` are generated from
  `describe` with the maelys-cli reference generator; `make contract-check`
  rejects a stale copy in CI.
