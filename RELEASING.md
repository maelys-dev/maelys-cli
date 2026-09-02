# Releasing

The 0.x series is source compatible within ABI 1 of `libmaelys_cli`. A release
requires:

1. `VERSION`, `MAELYS_CLI_VERSION` in `include/maelys/cli/version.h` and the
   changelog agree (`make check-version`).
2. `make check`, `make asan-ubsan`, `make analyze` and `make install-check`
   pass on macOS (Apple Silicon) and Linux (amd64/arm64).
3. `make generate-cli-reference` produces no diff in `docs/cli-reference.md`
   and `docs/cli-contract.json`.
4. Any change to the envelope, `describe` shape, error codes or exit codes is
   documented in `docs/command-conventions.md` and, when incompatible, bumps
   `MAELYS_CLI_CONTRACT` and `MAELYS_CLI_SCHEMA_VERSION` together.
5. Any change to the extension manifest bumps `MAELYS_CLI_API` and
   `MAELYS_CLI_EXTENSION_SCHEMA` together.
6. The annotated `vX.Y.Z` tag names the exact validated commit.
