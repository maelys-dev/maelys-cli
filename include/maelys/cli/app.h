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
 * describe, completion, rendering, delegation and exit codes. Handlers
 * receive a context and must report through maelys_cli_succeed() or
 * maelys_cli_fail().
 *
 * Environment: MAELYS_CLI_FORMAT=text|json selects the default rendering
 * when no --format/--json option is given. For protocol-stream commands it
 * only affects the failure envelope written to stderr, since their stdout
 * belongs to the protocol.
 */

/* The colors of help, on a terminal that accepts them: whether any is shown
 * is decided by --color, NO_COLOR, TERM=dumb and CLICOLOR_FORCE, never by
 * the product, and data.text is never colored. Each member is the
 * parameters of one SGR sequence, digits and ';' only, at most 16 bytes:
 * "1" bold, "36" cyan, "1;34" bold blue. NULL keeps the framework's choice
 * for that role, "" shows it plain. The framework's choice is bold
 * headings and nothing else. */
typedef struct maelys_cli_help_palette {
    const char *heading;      /* section titles: USAGE, COMMANDS, OPTIONS... */
    const char *command;      /* a command's pattern in a list of commands */
    const char *option;       /* an option's name, --name */
    const char *placeholder;  /* what the reader replaces: VALUE, FILE, a|b */
    const char *example;      /* a line of an example, to copy */
} maelys_cli_help_palette_t;

/* One human text in another language. `source` is the text exactly as the
 * catalog or the framework writes it in the canonical language, English: a
 * purpose, a summary, a heading, or the template of an error message with
 * its conversions (`Machine %s is unknown.`). `text` says the same in the
 * language, with the same conversions in the same order, which the catalog
 * validation checks. A text no translation names is shown as written. */
typedef struct maelys_cli_translation {
    const char *source;
    const char *text;
} maelys_cli_translation_t;

/* The translations of one language: its tag, well formed as RFC 5646
 * writes it (`fr`, `fr-CA`), and its texts. A language a product declares
 * is one it has: the line's request resolves among them (RFC 4647 Lookup),
 * the framework's own texts follow, and a text without translation stays
 * in English. A product that declares none is shown in English alone. */
/* At most this many languages besides English in one catalog. */
#define MAELYS_CLI_MAX_LANGUAGES 32u

typedef struct maelys_cli_language {
    const char *tag;
    const maelys_cli_translation_t *translations;
    size_t translation_count;
} maelys_cli_language_t;

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
    /* Optional text appended to the general help, for agents. Each of its
     * lines is a paragraph: help wraps it to its width at that line's own
     * indentation, so a paragraph is written on one line, however long, and
     * never broken by hand; a blank line stays a blank line. */
    const char *agent_guidance;
    void *user_data;
    /* The command a line that names none runs (agent-cli/v2, section 8, "A
     * line that names no command"): its identifier, or NULL for none. With
     * it, a line whose first word is an option, `--` or a word with one
     * dash, and an empty line, run that command; its operands are written
     * after `--`, and an unknown first word is still INVALID_COMMAND. It
     * must name a command of the product that is available, shown, not a
     * delegate, and a read, a stream or a transaction: the catalog
     * validation refuses any other, an `execute` command included, since a
     * line that names nothing is no consent to an action. */
    const char *default_command;
    /* The colors of help (maelys_cli_help_palette_t), or NULL for the
     * framework's: bold headings. The catalog validation refuses a member
     * that is not SGR parameters. */
    const maelys_cli_help_palette_t *help_palette;
    /* The languages the product's human text has besides English (help,
     * the manual page, the text rendering of a failure), or NULL. JSON and
     * describe never change with the language: their texts stay English. */
    const maelys_cli_language_t *languages;
    size_t language_count;
    /* The canonical language of the product's human text: the language its
     * catalog and its failure templates are written in, which `version`
     * names (`lang`) and from which `languages` translate; NULL for "en".
     * The framework's own texts follow it: a product written in French
     * (`"fr"`) shows French headings and messages without any table, and
     * `--lang` or a locale naming its canonical language, `C` included,
     * gives that language. A tag of the form language[-Script][-REGION]. */
    const char *canonical_language;
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
    int records_buffered;    /* text records held for the tabular form */
    int progress_shown;      /* a transient progress line is on stderr */
    int pager_pid;           /* the pager child, or 0 */
    FILE *pager_out;         /* stdout of the process while paging */
    int expect_checked;      /* maelys_cli_expect() was called */
    /* The language of human text this invocation resolved: the product's
     * translations and the framework's, NULL for English. */
    const maelys_cli_language_t *language;
    const maelys_cli_language_t *framework_language;
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
/* Typed operand values, 1 when the operand exists and is typed. */
int maelys_cli_operand_unsigned(
    const maelys_cli_context_t *context, size_t index, uint64_t *out);
int maelys_cli_operand_integer(
    const maelys_cli_context_t *context, size_t index, int64_t *out);
int maelys_cli_operand_choice(
    const maelys_cli_context_t *context, size_t index, size_t *out_index);
/* Raw text of an explicitly given option, NULL when absent. */
const char *maelys_cli_option(const maelys_cli_context_t *context, const char *name);
/* Explicit value, else the declared default_text, else fallback. */
const char *maelys_cli_option_or(
    const maelys_cli_context_t *context, const char *name, const char *fallback);
int maelys_cli_flag(const maelys_cli_context_t *context, const char *name);
/* Typed values: 1 when a value is delivered, from the invocation or from
 * the option's validated default_text; 0 when neither exists. Test
 * maelys_cli_option() != NULL to know whether the user gave it. */
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

/* Trunk diagnostics of spec 2.3, on stderr, in text mode only; both write
 * nothing under --format json or jsonl so the envelope stays alone. */
/* True when --verbose is active in text mode. */
int maelys_cli_verbose(const maelys_cli_context_t *context);
/* One detail line, `PROGRAM: message`, when maelys_cli_verbose(); never a
 * failure rendering (`PROGRAM: [`). */
void maelys_cli_detail(maelys_cli_context_t *context, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
/* True when progress may be shown: text mode and --progress always, or auto
 * with stderr a terminal. */
int maelys_cli_progress_wanted(const maelys_cli_context_t *context);
/* Transient progress: on a terminal the line is rewritten in place and
 * erased by maelys_cli_progress_done(); on another stderr (--progress
 * always) each call is one line. Nothing when progress is not wanted. */
void maelys_cli_progress(maelys_cli_context_t *context, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void maelys_cli_progress_done(maelys_cli_context_t *context);

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

/* Trusted variants: the JSON text comes from a serializer that already
 * guarantees validity (a JSON library), so it is neither validated nor
 * reformatted in compact and jsonl modes; it is written verbatim. Invalid
 * text would corrupt the output, so use them only behind such a serializer.
 * Indented mode still reformats. */
int maelys_cli_succeed_trusted(
    maelys_cli_context_t *context, const char *data_json, const char *human,
    int exit_code);
int maelys_cli_emit_record_trusted(
    maelys_cli_context_t *context, const char *record_json,
    const char *human_line);

/* Emits one record for MAELYS_CLI_OUTPUT_RECORDS commands. In jsonl mode
 * the object is one line, held until maelys_cli_finish_records() and never
 * written if the command fails: a failure leaves stdout empty in every
 * format. In json mode it is collected into data.records; in text mode
 * human_line is printed. Output that must flow while the work goes on is a
 * protocol stream, not records. */
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
/* maelys_cli_fail_error() with the detail agent-cli/v2 admits in the `error`
 * object of the envelope (schemas/envelope.json), as JSON text; NULL adds
 * nothing.
 *
 * `issues_json` is an array of objects `{"code", "message", "path"}`: the
 * causes under the code, each with a code of its own, what went wrong and,
 * optionally, where (a string, or an array of strings and integers). It is
 * where a product's domain detail goes under a code of the contract:
 * VALIDATION_FAILED with `{"code":"PANES_CONFIG","path":"panes.conf:12",
 * "message":"Unknown key 'colour'."}`.
 *
 * `extensions_json` is an object of extension members, each named `x-` and
 * then lowercase letters, digits and dashes, starting with a letter or a
 * digit, given once: machine detail an agent may act on, such as the
 * revisions of a REVISION_CONFLICT (`x-revision-seen`, `x-revision-current`)
 * or a delay before retrying. A member that repeats the code says the code
 * was ill chosen. Both go into JSON that agents log: no absolute path, no
 * environment and no file content without a reason.
 *
 * error->code is a stable code, or a code of the product's own domain
 * (`^[A-Z][A-Z0-9_]*$`, agent-cli/v2 section 8), which it documents; keep
 * the stable one when the boundary is among them. JSON and JSONL carry the
 * detail after code, message and hint; the text rendering, `PROGRAM: [CODE]
 * message` and `Hint:`, is unchanged. A code, an issue or a member that
 * breaks these rules is a defect of the command: the line is answered
 * UNEXPECTED instead, never with an envelope the contract refuses. At most
 * 64 issues and 64 members. */
int maelys_cli_fail_error_extended(
    maelys_cli_context_t *context, const maelys_cli_error_t *error,
    const char *issues_json, const char *extensions_json);
/* Failure from an errno left by maelys/cli/files.h: the code comes from
 * maelys_cli_file_error_code(), the message is `what: strerror(errno)` and
 * the hint is the explanation the file function returned (or a generic one
 * when NULL). Saves every product the same errno-to-code table. */
int maelys_cli_fail_file(
    maelys_cli_context_t *context, int saved_errno, const char *explanation,
    const char *what);

/* Diagnostics that never touch stdout. */
void maelys_cli_warn(maelys_cli_context_t *context, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Binds --apply to the reviewed plan (MAELYS_CLI_EXPECT_OPTION, catalog.h).
 * `fingerprint` is the `sha256:HEX` the handler has just computed, with
 * maelys_cli_fingerprint_*(), for the action it is about to perform on the
 * state as it now is. Returns 0 when --expect was not given or names that
 * fingerprint: the handler goes on, and puts the same string in
 * data.fingerprint. Otherwise replies PRECONDITION_FAILED, the hint saying
 * to plan again, and returns the exit code the handler returns at once:
 *
 *     int refused = maelys_cli_expect(context, fingerprint);
 *     if (refused) return refused;
 *
 * Call it before the first write, in plan mode as well as under --apply: a
 * caller that reads PRECONDITION_FAILED concludes that nothing changed. */
int maelys_cli_expect(maelys_cli_context_t *context, const char *fingerprint);

/* Reads a prompt answer only when interactive; refuses under
 * --non-interactive with a VALIDATION_FAILED error. */
int maelys_cli_confirm(
    maelys_cli_context_t *context, const char *question, int *out_confirmed);

#ifdef __cplusplus
}
#endif

#endif
