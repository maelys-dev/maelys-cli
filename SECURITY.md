# Security policy

Security fixes are provided for the latest published 0.x minor release.

Do not open a public issue for a suspected vulnerability. Use GitHub private
vulnerability reporting and include the affected version, operating system,
architecture, a minimal reproducer and the expected impact.

## Boundary

`libmaelys_cli` parses untrusted command-line arguments, renders output and
starts external programs on behalf of a product CLI. Its security claims are:

- every argument is validated against the declared catalog before a handler
  runs; unknown, duplicated, malformed or out-of-range inputs are refused;
- successful data is written to stdout only and failures to stderr only, so a
  caller can never mistake a diagnostic for data; human diagnostics render
  control bytes visibly instead of passing them to the terminal;
- files are read within explicit size bounds, counted on the bytes actually
  read, and written through private temporaries published atomically with
  an explicit replacement policy; configuration, manifests and secrets are
  judged on the descriptor that is read (`maelys_cli_read_trusted_file`), so
  no link, replacement, growth or FIFO between a check and a read changes
  what is trusted, and a secret buffer is zeroed on failure;
- external programs are started from absolute paths that must be regular,
  owned by root or the caller and not writable by group or world, through
  `execve` without a shell or PATH lookup. The checked executable and its
  trusted immediate parent remain open until `exec` (Linux executes binaries
  directly by descriptor), script shebangs require an absolute interpreter
  not named `env`, and non-standard descriptors are close-on-exec. Only the
  direct shebang is inspected: an absolute interpreter that is itself a script
  can delegate again, and its contents are not recursively inspected;
- the `maelys` dispatcher accepts external commands only from manifests in
  fixed directories that satisfy the same ownership and mode rules, with an
  optional SHA-256 pin of the executable, and refuses duplicates and
  unsupported `cliApi` values; an executable alias is canonicalized before
  checking, hashing and storing it. Manifest metadata shown in text output
  refuses terminal control characters. Manifests are parsed by maelys-json
  under a 64 KiB, depth 8, 1024-token budget, with duplicate members and
  invalid UTF-8 rejected;
- `maelys agents install` opens each project-relative parent directory without
  following symbolic links and publishes managed files relative to those
  descriptors, so a link cannot redirect a write outside the project;
- `maelys-cli-embed --define` treats names and values as literal bytes and
  never constructs a shell or `sed` program from them;
- JSON written by the framework is always well-formed: the writer refuses
  invalid UTF-8 rather than emitting it;
- `MAELYS_COMMANDS_PATH` overrides the manifest directories for development
  and tests; the same file checks apply, but a process that controls the
  environment of `maelys` can direct it to its own manifests. Do not rely on
  the dispatcher as a privilege boundary against the invoking user.

The framework is not a sandbox, a policy engine or an authentication layer.
Product decisions (what a command may do) belong to the product handler.
