#ifndef MAELYS_CLI_APP_H
#define MAELYS_CLI_APP_H

#include "maelys/cli/catalog.h"
#include "maelys/cli/invocation.h"
#include "maelys/cli/json.h"
#include "maelys/cli/terminal.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A product CLI declares one application: identity, catalog and helper
 * locations. maelys_cli_main() then owns argv parsing, help, version,
 * describe, rendering, delegation and exit codes. Handlers receive a context
 * and must report through maelys_cli_succeed() or maelys_cli_fail().
 */

typedef struct maelys_cli_app {
    const char *program;   /* executable name shown in usage */
    const char *product;   /* human product name */
    const char *version;
    const char *summary;   /* one line under the program name */
    const maelys_cli_command_t *commands;
    size_t command_count;
    /* Absolute directories searched for delegate helpers, in order, after
     * the directory of the running executable and its ../libexec/PROGRAM. */
    const char *const *helper_directories;
    size_t helper_directory_count;
    /* Optional free text appended to help for agents. */
    const char *agent_guidance;
    void *user_data;
} maelys_cli_app_t;

typedef struct maelys_cli_context {
    const maelys_cli_app_t *app;
    const maelys_cli_invocation_t *invocation;
    maelys_cli_terminal_t terminal;
    FILE *out;
    FILE *err;
    void *user_data;
    const char *executable;  /* argv[0] as received by maelys_cli_main, or NULL */
    /* private */
    maelys_cli_json_writer_t records;
    size_t record_count;
    int records_failed;
    int replied;
} maelys_cli_context_t;

/* Process entry point: returns the exit code. Uses stdout and stderr. */
int maelys_cli_main(const maelys_cli_app_t *app, int argc, char **argv);

/* Same as maelys_cli_main with explicit streams, for tests. argv excludes
 * the program name. */
int maelys_cli_run(
    const maelys_cli_app_t *app, int argc, char **argv, FILE *out, FILE *err);

/* Verifies the catalog: identifiers, summaries, schema JSON, choices,
 * ranges, --apply presence for transactions, handler or delegate presence.
 * Returns 0 or -1 with error. */
int maelys_cli_catalog_validate(
    const maelys_cli_app_t *app, maelys_cli_error_t *error);

/* Built-in commands prepended to every catalog (help, version, describe). */
const maelys_cli_command_t *maelys_cli_builtin_commands(size_t *out_count);

/* Convenience accessors for handlers. */
const char *maelys_cli_operand(const maelys_cli_context_t *context, size_t index);
size_t maelys_cli_operand_count(const maelys_cli_context_t *context);
const char *maelys_cli_option(const maelys_cli_context_t *context, const char *name);
const char *maelys_cli_option_or(
    const maelys_cli_context_t *context, const char *name, const char *fallback);
int maelys_cli_flag(const maelys_cli_context_t *context, const char *name);
int maelys_cli_option_unsigned(
    const maelys_cli_context_t *context, const char *name, uint64_t *out);
int maelys_cli_option_integer(
    const maelys_cli_context_t *context, const char *name, int64_t *out);
int maelys_cli_option_choice(
    const maelys_cli_context_t *context, const char *name, size_t *out_index);
size_t maelys_cli_option_count(
    const maelys_cli_context_t *context, const char *name);
const char *maelys_cli_option_at(
    const maelys_cli_context_t *context, const char *name, size_t occurrence);
int maelys_cli_json_mode(const maelys_cli_context_t *context);
int maelys_cli_non_interactive(const maelys_cli_context_t *context);

/* 1 once a reply (success, records or failure) has been emitted. Helpers
 * that may reply return the exit code; callers test this before replying
 * themselves. */
int maelys_cli_replied(const maelys_cli_context_t *context);

/* Finds a trusted helper executable by name using the delegate search order:
 * beside the running executable, ../libexec/PROGRAM, ../libexec, then the
 * application's helper_directories. Returns -1 with errno ENOENT. */
int maelys_cli_resolve_helper(
    const maelys_cli_context_t *context, const char *name, char *out_path,
    size_t out_size);

/* Emits success data. data_json is a JSON object (validated) or NULL for
 * {}. human is printed in text mode; NULL prints the indented data instead.
 * Returns exit_code, or MAELYS_CLI_EXIT_FAILURE when emission failed. */
int maelys_cli_succeed(
    maelys_cli_context_t *context, const char *data_json, const char *human,
    int exit_code);

/* Same, with the data taken from a writer that is finished and cleared. */
int maelys_cli_succeed_writer(
    maelys_cli_context_t *context, maelys_cli_json_writer_t *data,
    const char *human, int exit_code);

/* Emits one record for MAELYS_CLI_OUTPUT_RECORDS commands. In jsonl mode
 * the object is written immediately as one line; in json mode it is
 * collected into data.records; in text mode human_line is printed. */
int maelys_cli_emit_record(
    maelys_cli_context_t *context, const char *record_json,
    const char *human_line);

/* Terminates a records command: data becomes {count, records}. */
int maelys_cli_finish_records(maelys_cli_context_t *context, int exit_code);

/* Emits a failure envelope on stderr and returns MAELYS_CLI_EXIT_FAILURE. */
int maelys_cli_fail(
    maelys_cli_context_t *context, const char *code, const char *hint,
    const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
int maelys_cli_fail_errno(
    maelys_cli_context_t *context, const char *code, int saved_errno,
    const char *what);
int maelys_cli_fail_error(
    maelys_cli_context_t *context, const maelys_cli_error_t *error);

/* Diagnostics that never touch stdout. */
void maelys_cli_warn(maelys_cli_context_t *context, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Reads a prompt answer only when interactive; refuses under
 * --non-interactive with a VALIDATION_FAILED error. */
int maelys_cli_confirm(
    maelys_cli_context_t *context, const char *question, int *out_confirmed);

#ifdef __cplusplus
}
#endif

#endif
