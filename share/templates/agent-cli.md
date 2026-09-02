# Using @PROGRAM@ from an agent

<!-- Template installed by maelys-cli under share/maelys-cli/templates/
     (CC0-1.0, edit freely).
     Copy it to docs/agent-cli.md and replace the placeholders. The generic
     agent contract is in agent-cli.md of the maelys-cli distribution; keep
     only product specifics here. -->

Discover the executable contract instead of guessing flags from prose:

```sh
@PROGRAM@ describe --summary --format json --compact --non-interactive
@PROGRAM@ describe COMMAND_ID --format json --compact --non-interactive
```

Every call in automation uses `--format json --non-interactive`. Success is
one envelope on stdout; failure is one envelope on stderr with a stable
`error.code` and an actionable `error.hint`. Exit `2` means the command
completed and its report contains violations: read `data`.

## Typical sequence

<!-- The two or three commands an agent runs in order for this product,
     with the fields it must read between them, for example:

```sh
@PROGRAM@ config validate --config FILE --format json --compact --non-interactive
@PROGRAM@ serve --config FILE
```

For a stream command, state what the agent waits for and how it detects
failure (`event == "ready"`, `fatal`, process exit). -->

## Product-specific preconditions

<!-- Digests, revisions or identifiers that a plan returns and that the
     agent must pass back with --apply. -->

For repository changes, agents follow the instructions installed by
`maelys agents install` (`AGENTS.md`, `CLAUDE.md`, `docs/maelys-cli-guide.md`).
