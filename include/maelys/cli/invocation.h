#ifndef MAELYS_CLI_INVOCATION_H
#define MAELYS_CLI_INVOCATION_H

#include "maelys/cli/catalog.h"
#include "maelys/cli/terminal.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_CLI_MAX_OPERANDS 256u
#define MAELYS_CLI_MAX_OPTIONS 64u
#define MAELYS_CLI_MAX_OPTION_NAME 64u

typedef enum maelys_cli_format {
    MAELYS_CLI_FORMAT_TEXT = 0,
    MAELYS_CLI_FORMAT_JSON = 1,
    MAELYS_CLI_FORMAT_JSONL = 2
} maelys_cli_format_t;

typedef struct maelys_cli_parsed_option {
    const maelys_cli_option_t *descriptor; /* command or transport option */
    int transport;              /* 1 for --format, --json, ... */
    const char *name;
    const char *value;          /* raw text, NULL for flags */
    int boolean_value;
    uint64_t unsigned_value;    /* UNSIGNED, SIZE, DURATION */
    int64_t signed_value;       /* INTEGER */
    size_t choice_index;        /* CHOICE */
} maelys_cli_parsed_option_t;

#define MAELYS_CLI_MAX_ERROR_MESSAGE 4096u
#define MAELYS_CLI_MAX_ERROR_HINT 1024u

typedef struct maelys_cli_error {
    char code[32];
    char message[MAELYS_CLI_MAX_ERROR_MESSAGE];
    char hint[MAELYS_CLI_MAX_ERROR_HINT];
} maelys_cli_error_t;

/* Stable error codes shared by every Maelys CLI. */
#define MAELYS_CLI_CODE_INVALID_COMMAND "INVALID_COMMAND"
#define MAELYS_CLI_CODE_VALIDATION_FAILED "VALIDATION_FAILED"
#define MAELYS_CLI_CODE_PRECONDITION_FAILED "PRECONDITION_FAILED"
#define MAELYS_CLI_CODE_POLICY_FAILED "POLICY_FAILED"
#define MAELYS_CLI_CODE_ACCESS_DENIED "ACCESS_DENIED"
#define MAELYS_CLI_CODE_NOT_FOUND "NOT_FOUND"
#define MAELYS_CLI_CODE_IO_FAILED "IO_FAILED"
#define MAELYS_CLI_CODE_PROCESS_FAILED "PROCESS_FAILED"
#define MAELYS_CLI_CODE_PROTOCOL_FAILED "PROTOCOL_FAILED"
#define MAELYS_CLI_CODE_UNSUPPORTED "UNSUPPORTED"
#define MAELYS_CLI_CODE_UNEXPECTED "UNEXPECTED"

/* Process exit codes of the agent-cli/v2 contract. */
#define MAELYS_CLI_EXIT_OK 0
#define MAELYS_CLI_EXIT_FAILURE 1
#define MAELYS_CLI_EXIT_VIOLATIONS 2

typedef struct maelys_cli_invocation {
    const maelys_cli_command_t *command;
    const char *operands[MAELYS_CLI_MAX_OPERANDS];
    size_t operand_count;
    /* Typed values of operands whose descriptor declares a kind; indexes
     * follow `operands`. descriptor is NULL for untyped operands. */
    maelys_cli_parsed_option_t operand_values[MAELYS_CLI_MAX_OPERANDS];
    maelys_cli_parsed_option_t options[MAELYS_CLI_MAX_OPTIONS];
    size_t option_count;
    maelys_cli_format_t format;
    int compact;
    int non_interactive;
    maelys_cli_color_mode_t color;
    int help_requested;
    int rendering_requested; /* any --format/--json/--compact/--pretty */
    int pattern_words;       /* argv entries consumed by the pattern */
    /* Trunk options of spec 2.3: diagnostics and paging. Progress and pager
     * use maelys_cli_tristate_t; verbose is a flag. --pager is a rendering
     * option (refused by protocol streams); --progress and --verbose are
     * not (accepted by every command, forwarded verbatim by delegates). */
    int verbose;
    int progress;            /* maelys_cli_tristate_t */
    int pager;               /* maelys_cli_tristate_t */
    int pager_requested;     /* --pager given explicitly */
} maelys_cli_invocation_t;

/* auto | always | never, in the order of the trunk's choices. */
typedef enum maelys_cli_tristate {
    MAELYS_CLI_AUTO = 0,
    MAELYS_CLI_ALWAYS = 1,
    MAELYS_CLI_NEVER = 2
} maelys_cli_tristate_t;

void maelys_cli_error_set(
    maelys_cli_error_t *error, const char *code, const char *hint,
    const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

/* Fills an error from errno for I/O boundaries. */
void maelys_cli_error_from_errno(
    maelys_cli_error_t *error, const char *code, int saved_errno,
    const char *what);

struct maelys_cli_app;

/* Resolves the command and validates options and operands in causal order:
 * command, option spelling and duplicates, option values, dependencies and
 * conflicts, required options, operand arity, then rendering constraints.
 * Returns 0 on success and -1 with a populated error. */
int maelys_cli_parse(
    const struct maelys_cli_app *app, int argc, char **argv,
    maelys_cli_invocation_t *out, maelys_cli_error_t *error);

const char *maelys_cli_invocation_operand(
    const maelys_cli_invocation_t *invocation, size_t index);
const maelys_cli_parsed_option_t *maelys_cli_invocation_operand_value(
    const maelys_cli_invocation_t *invocation, size_t index);
const maelys_cli_parsed_option_t *maelys_cli_invocation_option(
    const maelys_cli_invocation_t *invocation, const char *name);
const maelys_cli_parsed_option_t *maelys_cli_invocation_option_at(
    const maelys_cli_invocation_t *invocation, const char *name,
    size_t occurrence);
size_t maelys_cli_invocation_option_count(
    const maelys_cli_invocation_t *invocation, const char *name);

#ifdef __cplusplus
}
#endif

#endif
