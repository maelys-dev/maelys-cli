# ABI and compatibility policy

`libmaelys_cli` is distributed as a static library with ABI generation 1
(`MAELYS_CLI_ABI`). During the 0.x series:

- source compatibility is preserved within a minor series; a patch release
  never changes a public signature or the meaning of a field;
- `maelys_cli_command_t`, `maelys_cli_option_t`, `maelys_cli_operand_t`
  and `maelys_cli_app_t` are positional aggregates initialized by product
  catalogs. New members are appended at the end and zero is always the
  neutral value, so existing positional initializers keep their meaning;
- `maelys_cli_invocation_t`, `maelys_cli_context_t` and
  `maelys_cli_json_writer_t` are allocated by callers but their members
  marked private may change; use the accessor functions;
- the `agent-cli/v2` envelope, `describe` shape, error codes and exit codes
  form the machine contract; incompatible changes bump
  `MAELYS_CLI_CONTRACT` and `MAELYS_CLI_SCHEMA_VERSION` together;
- the extension manifest and `MAELYS_CLI_API` change together;
- headers compile as C11 and C++17.

Promotion to a cross-project shared object requires at least two migrated
consumers (Warden and Maelys Git), a stable `describe` reference for both,
and opaque handles for every caller-allocated structure whose layout may
still change.
