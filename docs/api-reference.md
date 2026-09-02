# libmaelys_cli API reference

Every public function of `libmaelys_cli`, grouped by header. Conventions:

- mechanics return `0` on success and `-1` on failure; when a system call
  is involved `errno` is set, otherwise the failure is a refused input;
- output parameters are untouched on failure unless stated otherwise;
- buffers returned through `out` pointers are owned by the caller and
  released with `free()`, except where a dedicated `_clear` or `_free`
  function exists;
- handlers return the value of the reply function they called.

`scripts/api-doc-check.sh` verifies that every public function declared in
`include/maelys/cli/*.h` is named in this file.

## `maelys/cli/version.h`

| Symbol | Contract |
| --- | --- |
| `MAELYS_CLI_VERSION`, `_MAJOR`, `_MINOR`, `_PATCH` | Library version, kept equal to `VERSION`. |
| `MAELYS_CLI_ABI` | Link-level ABI generation (1). |
| `MAELYS_CLI_CONTRACT`, `MAELYS_CLI_SCHEMA_VERSION` | Envelope contract `agent-cli/v2`, schema version 2. |
| `MAELYS_CLI_API`, `MAELYS_CLI_EXTENSION_SCHEMA` | Dispatcher/extension contract (1, `maelys.cli-extension/v1`). |
| `const char *maelys_cli_version(void)` | Returns `MAELYS_CLI_VERSION`. |

## `maelys/cli/values.h`

All parsers refuse `NULL`, empty text, leading `+`/`-` where a sign is not
meaningful, surrounding whitespace, trailing characters, overflow and any
value outside the inclusive `[minimum, maximum]`. `minimum > maximum` is
refused.

| Function | Contract |
| --- | --- |
| `int maelys_cli_parse_u64_decimal(const char *value, uint64_t minimum, uint64_t maximum, uint64_t *out)` | Decimal digits only. |
| `int maelys_cli_parse_u32_decimal(const char *value, uint32_t minimum, uint32_t maximum, uint32_t *out)` | Same through the `u64` parser. |
| `int maelys_cli_parse_i64_decimal(const char *value, int64_t minimum, int64_t maximum, int64_t *out)` | Optional leading `-`; `INT64_MIN` accepted. |
| `int maelys_cli_parse_byte_size(const char *value, uint64_t minimum, uint64_t maximum, uint64_t *out_bytes)` | Digits with one optional suffix `K`, `M`, `G`, `T` (either case), powers of 1024; multiplication overflow refused. `"0"` is valid when `minimum` is 0. |
| `int maelys_cli_parse_duration_ms(const char *value, uint64_t minimum, uint64_t maximum, uint64_t *out_ms)` | Digits with a mandatory unit `ms`, `s`, `m`, `h` or `d`; result in milliseconds. A bare number is refused. |
| `int maelys_cli_parse_boolean(const char *value, int *out)` | Accepts `true`/`false`, `yes`/`no`, `on`/`off`, `1`/`0`. |
| `int maelys_cli_parse_choice(const char *value, const char *const *choices, size_t *out_index)` | Exact match in a `NULL`-terminated array; index returned. |
| `int maelys_cli_parse_hex(const char *value, size_t digit_count)` | Lowercase hexadecimal of exactly `digit_count` digits. |
| `int maelys_cli_string_list_append(maelys_cli_string_list_t *list, const char *value)` | Appends a borrowed pointer (not copied). |
| `void maelys_cli_string_list_clear(maelys_cli_string_list_t *list)` | Frees the array, not the strings; resets to empty. |

## `maelys/cli/environment.h`

| Function | Contract |
| --- | --- |
| `int maelys_cli_environment_append(maelys_cli_environment_t *env, const char *assignment)` | `NAME=VALUE` copies the value (which may contain `=`); `NAME` alone imports `getenv(NAME)` and fails when undefined. Names match `[A-Za-z_][A-Za-z0-9_]*`. Both strings are copied and owned by the overlay. |
| `const char *maelys_cli_environment_get(const maelys_cli_environment_t *env, const char *name)` | Last value appended for `name`, or `NULL`. |
| `void maelys_cli_environment_clear(maelys_cli_environment_t *env)` | Frees every entry; resets to empty. |
| `int maelys_cli_environment_to_envp(const maelys_cli_environment_t *env, char ***out_envp)` | Builds a `NULL`-terminated `NAME=VALUE` array for `execve`. |
| `void maelys_cli_envp_free(char **envp)` | Releases an array from `to_envp`. |

## `maelys/cli/files.h`

| Function | Contract |
| --- | --- |
| `int maelys_cli_read_regular_file(const char *path, size_t minimum_size, size_t maximum_size, unsigned char **out_bytes, size_t *out_size)` | Opens with `O_CLOEXEC`, requires a regular file (`EFTYPE`/`EINVAL` otherwise) whose size lies in the bounds (`EFBIG` otherwise), reads it completely (`EIO` on short read). `*out_bytes` is `NULL` only for size 0. |
| `int maelys_cli_read_descriptor(int fd, size_t maximum_size, unsigned char **out_bytes, size_t *out_size)` | Reads until EOF, refusing more than `maximum_size` bytes with `EFBIG`. |
| `int maelys_cli_write_file_atomic(const char *path, const void *bytes, size_t size, mode_t mode, maelys_cli_write_policy_t policy)` | Writes a private `mkstemp` temporary in the destination directory, `fchmod(mode)`, `fsync`, then publishes it: `MAELYS_CLI_WRITE_REPLACE` renames over the target; `MAELYS_CLI_WRITE_NO_REPLACE` links it and fails with `EEXIST` when any entry (a dangling symlink included) already occupies the path. `mode` 0 and an invalid policy are `EINVAL`. The temporary is removed on failure. |
| `int maelys_cli_check_file(const char *path, unsigned int requirements, const char **out_error)` | Checks any combination of `MAELYS_CLI_FILE_REGULAR`, `_NO_SYMLINK` (`ELOOP`), `_OWNER_TRUSTED` (root or `geteuid()`, else `EPERM`), `_NOT_WRITABLE_BY_OTHERS` (`EPERM`), `_PRIVATE` (no group/world bits, `EPERM`), `_EXECUTABLE` (owner execute bit, `EACCES`). Missing path is `ENOENT`. `out_error` receives a short stable explanation. |

## `maelys/cli/digest.h`

| Function | Contract |
| --- | --- |
| `void maelys_cli_sha256_init(maelys_cli_sha256_t *ctx)` / `maelys_cli_sha256_update(ctx, bytes, size)` / `maelys_cli_sha256_final(ctx, unsigned char out[32])` | Incremental SHA-256 (FIPS 180-4). |
| `void maelys_cli_sha256_hex(const void *bytes, size_t size, char out[65])` | One-shot lowercase hexadecimal digest, NUL-terminated. |
| `int maelys_cli_sha256_file(const char *path, size_t maximum_size, char out[65])` | Digest of a regular file read through `maelys_cli_read_regular_file`; `-1` with its `errno`. |

## `maelys/cli/json.h`

Writer: builds one JSON document incrementally. Any failed step marks the
writer failed; `maelys_cli_json_finish()` then returns `NULL`.

| Function | Contract |
| --- | --- |
| `void maelys_cli_json_writer_init(maelys_cli_json_writer_t *w)` / `maelys_cli_json_writer_clear(w)` | Initialize / release and reset. |
| `int maelys_cli_json_begin_object(w)` / `maelys_cli_json_end_object(w)` / `maelys_cli_json_begin_array(w)` / `maelys_cli_json_end_array(w)` | Containers, at most `MAELYS_CLI_JSON_MAX_DEPTH` (64) deep. Commas are inserted automatically. |
| `int maelys_cli_json_key(w, const char *key)` | Only inside an object and never twice in a row. |
| `int maelys_cli_json_string(w, const char *value)` / `maelys_cli_json_stringn(w, value, length)` | Escapes `"`, `\`, control characters (`\n`, `\t`, `\uXXXX`); bytes are otherwise passed through. `NULL` is refused and fails the writer; use `maelys_cli_json_null()` for an explicit null. A value inside an object must follow a key. |
| `int maelys_cli_json_integer(w, int64_t)` / `maelys_cli_json_unsigned(w, uint64_t)` / `maelys_cli_json_boolean(w, int)` / `maelys_cli_json_null(w)` | Scalars. |
| `int maelys_cli_json_raw(w, const char *json)` | Inserts pre-serialized JSON after validating it; surrounding whitespace is trimmed. |
| `int maelys_cli_json_key_string(w, key, value)` / `maelys_cli_json_key_integer` / `maelys_cli_json_key_unsigned` / `maelys_cli_json_key_boolean` / `maelys_cli_json_key_raw` | Key followed by value. |
| `char *maelys_cli_json_finish(w)` | Returns the text (caller frees) when every container is closed, or `NULL`; the writer is reset either way. |

Reader:

| Function | Contract |
| --- | --- |
| `int maelys_cli_json_validate(const char *text, size_t length, size_t *out_offset)` | Strict RFC 8259 syntax of exactly one value, depth limit 64, control characters and bad escapes refused. `out_offset` receives the failing byte. UTF-8 and duplicate keys are not checked. |
| `int maelys_cli_json_format(const char *text, int compact, char **out)` | Re-serializes valid JSON compact or two-space indented, preserving key order. `EINVAL` on invalid input. |
| `int maelys_cli_json_object_get(const char *text, const char *key, const char **out_value, size_t *out_length)` | Raw value of a top-level member: `1` found, `0` absent, `-1` when `text` is not a valid object. The first occurrence wins; keys are compared undecoded. |
| `int maelys_cli_json_decode_string(const char *token, size_t length, char **out)` | Decodes a quoted token into UTF-8, including surrogate pairs; refuses the escaped NUL character (` `) and lone surrogates. |
| `int maelys_cli_json_decode_unsigned(const char *token, size_t length, uint64_t *out)` | Decimal digits only, overflow refused. |

## `maelys/cli/terminal.h`

| Function | Contract |
| --- | --- |
| `void maelys_cli_terminal_detect(maelys_cli_terminal_t *t, maelys_cli_color_mode_t mode)` | Fills tty flags, columns (`COLUMNS`, then `TIOCGWINSZ`, default 80) and per-stream color decision: `NEVER` wins, then `ALWAYS` or `CLICOLOR_FORCE`, then `NO_COLOR` or `TERM=dumb` disable, otherwise `isatty()`. |
| `const char *maelys_cli_style(int enabled, maelys_cli_style_t style)` | ANSI sequence when `enabled`, else `""`. |

## `maelys/cli/process.h`

| Function | Contract |
| --- | --- |
| `int maelys_cli_process_check_executable(const char *path, const char **out_error)` | Absolute path (`EINVAL`), regular, owned by root or the caller, not group/world writable, owner-executable; see `maelys_cli_check_file`. |
| `int maelys_cli_process_run(const char *path, char *const argv[], char *const envp[], maelys_cli_process_status_t *out_status)` | Checks the executable, `fork`, resets signals, closes every descriptor above 2 without `FD_CLOEXEC`, `execve` (`envp` `NULL` inherits `environ`), waits. An `exec` failure is reported to the parent as `-1` with the child's `errno`. Standard descriptors are inherited. |
| `int maelys_cli_process_replace(const char *path, char *const argv[], char *const envp[])` | Same checks, flushes stdio, then `execve` in place; returns `-1` only on failure. |
| `int maelys_cli_process_exit_code(const maelys_cli_process_status_t *status)` | Exit status, or `128 + signal`. |
| `int maelys_cli_process_resolve(const char *name, const char *const *directories, size_t count, char *out_path, size_t out_size)` | Finds `name` (no `/`) as a trusted executable in absolute directories, in order; `ENOENT` when absent, `ENAMETOOLONG` when the buffer is too small. Never consults `PATH`. |
| `int maelys_cli_executable_directory(const char *argv0, char *out, size_t size)` | Directory of the running binary via `_NSGetExecutablePath` (macOS) or `/proc/self/exe` (Linux), falling back to `realpath(argv0)` when it contains a `/`. |

## `maelys/cli/catalog.h`

Types `maelys_cli_operand_t`, `maelys_cli_option_t`, `maelys_cli_command_t`
and the declaration macros are described in `docs/command-conventions.md`
and the agent guide.

| Function | Contract |
| --- | --- |
| `const char *maelys_cli_value_kind_name(maelys_cli_value_kind_t)` | `boolean`, `string`, `integer`, `unsigned`, `size`, `duration`, `path`, `choice`, `hex`, `absolute-path`, `digest`. |
| `size_t maelys_cli_digest_hex_digits(const char *algorithm)` | 40 for `sha1`, 64 for `sha256`, 96 for `sha384`, 128 for `sha512`, 0 otherwise. Used by the `digest` kind (`ALGORITHM:HEX`). |
| `const char *maelys_cli_effect_name(maelys_cli_effect_t)` | `none`, `read`, `preview`, `apply`, `commit`, `execute`, `stream`. |
| `const char *maelys_cli_output_mode_name(maelys_cli_output_mode_t)` | `json-envelope`, `json-records`, `protocol-stream`. |
| `int maelys_cli_command_synopsis(const maelys_cli_command_t *command, char *out, size_t size)` | Explicit `synopsis` when set, otherwise `pattern OPERAND [OPTIONAL] [REST...] [--option VALUE] --required VALUE [--repeatable TEXT...]`; choices without a `value_name` render as `a|b`. `-1` when truncated. |

## `maelys/cli/invocation.h`

| Function | Contract |
| --- | --- |
| `void maelys_cli_error_set(maelys_cli_error_t *error, const char *code, const char *hint, const char *format, ...)` | Fills code (default `UNEXPECTED`), optional hint and a formatted message; messages hold up to `MAELYS_CLI_MAX_ERROR_MESSAGE` (4096) bytes and hints `MAELYS_CLI_MAX_ERROR_HINT` (1024). |
| `void maelys_cli_error_from_errno(maelys_cli_error_t *error, const char *code, int saved_errno, const char *what)` | Message `what: strerror(errno)` with a generic hint. |
| `int maelys_cli_parse(const maelys_cli_app_t *app, int argc, char **argv, maelys_cli_invocation_t *out, maelys_cli_error_t *error)` | Resolves the command (built-ins first, longest pattern wins; `--help`/`-h` and `--version` map to `help` and `version`), then validates in causal order: option spelling and support, duplicates, value kind and range, `depends_on`/`conflicts_with`, required options, operand arity, stream rendering flags, `jsonl` availability. Delegate commands collect everything after the pattern as operands. `--` ends option parsing. `--help` after a command sets `help_requested` and skips the remaining validation. |
| `const char *maelys_cli_invocation_operand(const maelys_cli_invocation_t *inv, size_t index)` | Operand or `NULL`. |
| `const maelys_cli_parsed_option_t *maelys_cli_invocation_operand_value(inv, size_t index)` | Typed value of an operand whose descriptor declares a kind, `NULL` for untyped operands. |
| `const maelys_cli_parsed_option_t *maelys_cli_invocation_option(inv, const char *name)` / `maelys_cli_invocation_option_at(inv, name, occurrence)` | Parsed option (raw text plus typed value) or `NULL`. |
| `size_t maelys_cli_invocation_option_count(inv, name)` | Occurrences of a repeatable option. |

## `maelys/cli/app.h`

Entry points:

| Function | Contract |
| --- | --- |
| `int maelys_cli_main(const maelys_cli_app_t *app, int argc, char **argv)` | Process entry point: records `argv[0]` for helper resolution and calls `maelys_cli_run` with stdout/stderr. |
| `int maelys_cli_run(app, int argc, char **argv, FILE *out, FILE *err)` | `argv` excludes the program name. Validates the catalog, pre-scans rendering flags so that even a parse failure is rendered in the requested format, parses, runs help/delegate/handler and returns the exit code. A non-stream handler that never replies yields `UNEXPECTED`. `MAELYS_CLI_FORMAT=json\|text` supplies the default format when no rendering option is given; for stream commands it only shapes the stderr failure envelope. |
| `int maelys_cli_catalog_validate(app, maelys_cli_error_t *error)` | Identifier `[a-z0-9._-]+`, pattern words, purpose, effect, `apply_effect` rules (`preview` + `--apply`), output mode, handler xor delegate, delegates without options, operand order (required before optional, one trailing variadic), option names unique and not transport names, choices/hex/ranges, `depends_on`/`conflicts_with` targets, schema JSON object, unique ids and patterns across built-ins and product commands. |
| `const maelys_cli_command_t *maelys_cli_builtin_commands(size_t *out_count)` | `help`, `version`, `describe`, `completion` and the hidden `__complete` (id `complete.candidates`). |

Handler accessors (all read the validated invocation):

| Function | Contract |
| --- | --- |
| `const char *maelys_cli_operand(const maelys_cli_context_t *ctx, size_t index)` / `size_t maelys_cli_operand_count(ctx)` | Operands. |
| `int maelys_cli_operand_unsigned(ctx, index, uint64_t *out)` / `maelys_cli_operand_integer(ctx, index, int64_t *out)` / `maelys_cli_operand_choice(ctx, index, size_t *out_index)` | Typed operand values (`MAELYS_CLI_OPERAND_CHOICE`, `MAELYS_CLI_OPERAND_KIND`); `1` when the operand exists and is typed. |
| `const char *maelys_cli_option(ctx, const char *name)` / `maelys_cli_option_or(ctx, name, fallback)` | Raw value of the first occurrence, or `NULL` / fallback. |
| `int maelys_cli_flag(ctx, name)` | `1` when a flag is enabled (`--flag`, `--flag=true`), else `0`. |
| `int maelys_cli_option_unsigned(ctx, name, uint64_t *out)` / `maelys_cli_option_integer(ctx, name, int64_t *out)` / `maelys_cli_option_choice(ctx, name, size_t *out_index)` | `1` and the typed value when present, `0` otherwise. Unsigned covers `UNSIGNED`, `SIZE` and `DURATION` (milliseconds); the choice index of a `DIGEST` option is the index of its algorithm. |
| `size_t maelys_cli_option_count(ctx, name)` / `const char *maelys_cli_option_at(ctx, name, occurrence)` | Repeatable options. |
| `int maelys_cli_json_mode(ctx)` / `maelys_cli_non_interactive(ctx)` | Rendering flags. |
| `int maelys_cli_replied(ctx)` | `1` once success, records or failure has been emitted. A helper that may reply returns the exit code; the caller tests this before replying itself. |
| `int maelys_cli_resolve_helper(ctx, const char *name, char *out_path, size_t out_size)` | Trusted helper lookup with the delegate order: beside the running executable (`ctx->executable`, then the recorded `argv[0]`), `../libexec/PROGRAM`, `../libexec`, then `helper_directories`. `ENOENT` when absent. |

Replies (exactly one per handler; a second call is ignored and returns the given code):

| Function | Contract |
| --- | --- |
| `int maelys_cli_succeed(ctx, const char *data_json, const char *human, int exit_code)` | `data_json` must be a valid JSON object (`NULL` = `{}`), otherwise an `UNEXPECTED` failure is emitted. Text mode prints `human` (newline added) or the indented data when `human` is `NULL`. JSON mode prints the success envelope. Returns `exit_code` (typically `MAELYS_CLI_EXIT_OK` or `MAELYS_CLI_EXIT_VIOLATIONS`). |
| `int maelys_cli_succeed_writer(ctx, maelys_cli_json_writer_t *data, human, exit_code)` | Same with a writer that is finished and released. |
| `int maelys_cli_emit_record(ctx, const char *record_json, const char *human_line)` | `json-records` commands only. `jsonl`: one compact line immediately; `json`: collected into `data.records`; text: `human_line` or the indented record. `-1` on invalid JSON or I/O failure. |
| `int maelys_cli_finish_records(ctx, int exit_code)` | Terminates a records command; in JSON mode `data` is `{"count": N, "records": [...]}`. |
| `int maelys_cli_fail(ctx, const char *code, const char *hint, const char *format, ...)` | Failure envelope on stderr (JSON) or `program: [CODE] message` + `Hint:`; returns `MAELYS_CLI_EXIT_FAILURE`. |
| `int maelys_cli_fail_errno(ctx, code, int saved_errno, const char *what)` / `maelys_cli_fail_error(ctx, const maelys_cli_error_t *)` | Same from `errno` or a prepared error. |
| `void maelys_cli_warn(ctx, const char *format, ...)` | `program: warning: ...` on stderr; never touches stdout. |
| `int maelys_cli_confirm(ctx, const char *question, int *out_confirmed)` | Prompts on stderr and reads stdin when interactive; under `--non-interactive` or without a terminal it emits `VALIDATION_FAILED` and returns `-1`. |

## `maelys/cli/extension.h`

| Function | Contract |
| --- | --- |
| `const char *const *maelys_cli_extension_default_directories(size_t *out_count)` | Compile-time `PREFIX/share/maelys/commands`, then Homebrew, `/usr/local` and `/usr` directories. |
| `int maelys_cli_extension_load(const char *manifest_path, maelys_cli_extension_t *out, maelys_cli_error_t *error)` | Absolute path; manifest regular, non-symlink, trusted owner, not group/world writable, at most 64 KiB, valid JSON object with `schema`, `command`, `executable`, `cliApi`, `version`, optional `summary` and `sha256`; executable checked with `maelys_cli_process_check_executable`; digest compared when declared. Error codes: `ACCESS_DENIED`, `PROTOCOL_FAILED`, `UNSUPPORTED`, `VALIDATION_FAILED`, `IO_FAILED`. |
| `int maelys_cli_extension_discover(const char *const *directories, size_t count, maelys_cli_extension_set_t *out, maelys_cli_error_t *error)` | Loads every `*.json` (dotfiles excluded) of each absolute directory in lexical order; missing directories are skipped; any invalid manifest or duplicate command fails the whole discovery and leaves an empty set. |
| `const maelys_cli_extension_t *maelys_cli_extension_find(const maelys_cli_extension_set_t *set, const char *command)` | Lookup by command name. |
| `void maelys_cli_extension_set_clear(maelys_cli_extension_set_t *set)` | Releases the set. |
