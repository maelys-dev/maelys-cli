#include "maelys/cli/app.h"
#include "maelys/cli/process.h"
#include "maelys/cli/values.h"
#include "maelys/cli/version.h"
#include "internal.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *maelys_cli_argv0 = NULL;

/* ---- built-in commands ------------------------------------------------ */

static int builtin_help(maelys_cli_context_t *context);
static int builtin_version(maelys_cli_context_t *context);
static int builtin_describe(maelys_cli_context_t *context);

static const maelys_cli_operand_t help_operands[] = {
    {"COMMAND_ID", "Stable command identifier whose help is shown.", 0, 0},
};
static const maelys_cli_operand_t describe_operands[] = {
    {"COMMAND_ID", "Stable command identifier returned by the catalog.", 0, 0},
};
static const maelys_cli_option_t describe_options[] = {
    {"summary", MAELYS_CLI_VALUE_NONE, NULL,
     "Return the compact command inventory without output schemas.",
     0, 0, NULL, NULL, NULL, 0u, 0u, 0, 0, 0u, NULL},
};

#define HELP_SCHEMA "{\"type\":\"object\",\"additionalProperties\":false," \
    "\"required\":[\"text\",\"commands\"],\"properties\":{\"text\":{\"type\":" \
    "\"string\"},\"commands\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}}}}"
#define VERSION_SCHEMA "{\"type\":\"object\",\"additionalProperties\":false," \
    "\"required\":[\"product\",\"program\",\"version\",\"contract\",\"cliApi\"," \
    "\"framework\"],\"properties\":{\"product\":{\"type\":\"string\"}," \
    "\"program\":{\"type\":\"string\"},\"version\":{\"type\":\"string\"}," \
    "\"contract\":{\"const\":\"agent-cli/v2\"},\"cliApi\":{\"type\":\"integer\"}," \
    "\"framework\":{\"type\":\"string\"}}}"
#define DESCRIBE_SCHEMA "{\"type\":\"object\",\"description\":\"Command " \
    "catalog, summary or single descriptor.\",\"additionalProperties\":true," \
    "\"required\":[\"schemaVersion\",\"kind\",\"program\",\"commands\"]}"

static const maelys_cli_command_t builtins[] = {
    {"help", "help", "Show the generated CLI guide or one command's help.",
     MAELYS_CLI_EFFECT_READ, MAELYS_CLI_EFFECT_NONE, MAELYS_CLI_OUTPUT_ENVELOPE,
     help_operands, MAELYS_CLI_COUNT(help_operands), NULL, 0u, HELP_SCHEMA,
     builtin_help, NULL, "help [COMMAND_ID] | --help", 0},
    {"version", "version", "Return product identity.",
     MAELYS_CLI_EFFECT_READ, MAELYS_CLI_EFFECT_NONE, MAELYS_CLI_OUTPUT_ENVELOPE,
     NULL, 0u, NULL, 0u, VERSION_SCHEMA, builtin_version, NULL,
     "version | --version", 0},
    {"describe", "describe",
     "Return the machine-readable catalog, summary or one descriptor.",
     MAELYS_CLI_EFFECT_READ, MAELYS_CLI_EFFECT_NONE, MAELYS_CLI_OUTPUT_ENVELOPE,
     describe_operands, MAELYS_CLI_COUNT(describe_operands), describe_options,
     MAELYS_CLI_COUNT(describe_options), DESCRIBE_SCHEMA, builtin_describe,
     NULL, NULL, 0},
};

const maelys_cli_command_t *maelys_cli_builtin_commands(size_t *out_count) {
    if (out_count) *out_count = MAELYS_CLI_COUNT(builtins);
    return builtins;
}

size_t maelys_cli_app_command_count(const maelys_cli_app_t *app) {
    return MAELYS_CLI_COUNT(builtins) + (app ? app->command_count : 0u);
}

const maelys_cli_command_t *maelys_cli_app_command_at(
    const maelys_cli_app_t *app, size_t index) {
    if (index < MAELYS_CLI_COUNT(builtins)) return &builtins[index];
    index -= MAELYS_CLI_COUNT(builtins);
    return app && index < app->command_count ? &app->commands[index] : NULL;
}

const maelys_cli_command_t *maelys_cli_app_find_command(
    const maelys_cli_app_t *app, const char *id) {
    size_t count = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        if (command->id && !strcmp(command->id, id)) return command;
    }
    return NULL;
}

/* ---- catalog validation ---------------------------------------------- */

static int valid_identifier(const char *id) {
    if (!id || !*id) return 0;
    for (const char *p = id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '-' || *p == '_'))
            return 0;
    }
    return 1;
}

static int valid_pattern(const char *pattern) {
    if (!pattern || !*pattern || *pattern == ' ' ||
        pattern[strlen(pattern) - 1u] == ' ')
        return 0;
    for (const char *p = pattern; *p; ++p) {
        if (*p == '-' && (p == pattern || p[-1] == ' ')) return 0;
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == ' '))
            return 0;
        if (*p == ' ' && p[1] == ' ') return 0;
    }
    return 1;
}

static int validate_command(
    const maelys_cli_app_t *app, const maelys_cli_command_t *command,
    maelys_cli_error_t *error) {
    static const char *hint = "Fix the command catalog declaration.";
    const char *id = command->id ? command->id : "(null)";
    if (!valid_identifier(command->id)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' has an invalid identifier.", id);
        return -1;
    }
    if (!valid_pattern(command->pattern)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' has an invalid pattern.", id);
        return -1;
    }
    if (!command->purpose || !*command->purpose) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' has no purpose.", id);
        return -1;
    }
    if (command->effect == MAELYS_CLI_EFFECT_NONE ||
        command->effect > MAELYS_CLI_EFFECT_STREAM) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' has no effect.", id);
        return -1;
    }
    if (command->apply_effect != MAELYS_CLI_EFFECT_NONE) {
        int has_apply = 0;
        for (size_t i = 0u; i < command->option_count; ++i)
            if (!strcmp(command->options[i].name, "apply")) has_apply = 1;
        if (command->effect != MAELYS_CLI_EFFECT_PREVIEW || !has_apply ||
            (command->apply_effect != MAELYS_CLI_EFFECT_APPLY &&
             command->apply_effect != MAELYS_CLI_EFFECT_COMMIT)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Transactional command '%s' must declare effect preview, "
                "apply_effect apply or commit, and an --apply option.", id);
            return -1;
        }
    }
    if (command->output > MAELYS_CLI_OUTPUT_STREAM) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' has an invalid output mode.", id);
        return -1;
    }
    if ((command->handler == NULL) == (command->delegate == NULL)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' needs exactly one of handler or delegate.", id);
        return -1;
    }
    if (command->delegate && command->option_count != 0u) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Delegate command '%s' cannot declare options; arguments are "
            "passed through.", id);
        return -1;
    }
    int seen_optional = 0;
    int seen_variadic = 0;
    for (size_t i = 0u; i < command->operand_count; ++i) {
        const maelys_cli_operand_t *operand = &command->operands[i];
        if (!operand->name || !*operand->name || !operand->summary ||
            !*operand->summary || seen_variadic ||
            (operand->required && seen_optional)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' declares operand %zu incorrectly.", id, i);
            return -1;
        }
        if (!operand->required) seen_optional = 1;
        if (operand->variadic) seen_variadic = 1;
    }
    size_t transport_count = 0u;
    const maelys_cli_option_t *transport = maelys_cli_transport_options(
        &transport_count);
    for (size_t i = 0u; i < command->option_count; ++i) {
        const maelys_cli_option_t *option = &command->options[i];
        if (!option->name || !*option->name || !option->summary ||
            !*option->summary || strchr(option->name, '=') ||
            strchr(option->name, ' ') || option->name[0] == '-') {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' declares option %zu incorrectly.", id, i);
            return -1;
        }
        for (size_t j = 0u; j < transport_count; ++j) {
            if (!strcmp(transport[j].name, option->name)) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' redeclares transport option --%s.",
                    id, option->name);
                return -1;
            }
        }
        for (size_t j = i + 1u; j < command->option_count; ++j) {
            if (!strcmp(command->options[j].name, option->name)) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' declares --%s twice.", id, option->name);
                return -1;
            }
        }
        if (option->kind > MAELYS_CLI_VALUE_HEX ||
            (option->kind == MAELYS_CLI_VALUE_CHOICE &&
             (!option->choices || !option->choices[0])) ||
            (option->kind == MAELYS_CLI_VALUE_HEX && option->hex_digits == 0u) ||
            (option->maximum && option->minimum > option->maximum) ||
            option->signed_minimum > option->signed_maximum) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' option --%s has an invalid value "
                "declaration.", id, option->name);
            return -1;
        }
        const char *references[2] = {option->requires, option->conflicts_with};
        for (size_t r = 0u; r < 2u; ++r) {
            if (!references[r]) continue;
            int found = 0;
            for (size_t j = 0u; j < command->option_count; ++j)
                if (j != i && !strcmp(command->options[j].name, references[r]))
                    found = 1;
            if (!found) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s references unknown "
                    "option --%s.", id, option->name, references[r]);
                return -1;
            }
        }
    }
    if (command->output_schema_json) {
        size_t offset = 0u;
        const char *first = command->output_schema_json;
        while (*first == ' ' || *first == '\n' || *first == '\t') ++first;
        if (*first != '{' || maelys_cli_json_validate(
                command->output_schema_json,
                strlen(command->output_schema_json), &offset) != 0) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' has an invalid output schema at byte "
                "%zu.", id, offset);
            return -1;
        }
    }
    (void)app;
    return 0;
}

int maelys_cli_catalog_validate(
    const maelys_cli_app_t *app, maelys_cli_error_t *error) {
    if (!app || !app->program || !*app->program || !app->product ||
        !*app->product || !app->version || !*app->version ||
        (app->command_count && !app->commands)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED,
            "Fill program, product, version and commands.",
            "Application declaration is incomplete.");
        return -1;
    }
    size_t count = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        if (validate_command(app, command, error) != 0) return -1;
        for (size_t j = i + 1u; j < count; ++j) {
            const maelys_cli_command_t *other = maelys_cli_app_command_at(app, j);
            if (!strcmp(command->id, other->id) ||
                !strcmp(command->pattern, other->pattern)) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED,
                    "Give every command a unique identifier and pattern.",
                    "Catalog command '%s' collides with '%s'.", command->id,
                    other->id);
                return -1;
            }
        }
    }
    return 0;
}

/* ---- accessors --------------------------------------------------------- */

const char *maelys_cli_operand(const maelys_cli_context_t *context, size_t index) {
    return context ? maelys_cli_invocation_operand(context->invocation, index) : NULL;
}

size_t maelys_cli_operand_count(const maelys_cli_context_t *context) {
    return context && context->invocation ? context->invocation->operand_count : 0u;
}

const char *maelys_cli_option(const maelys_cli_context_t *context, const char *name) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    return option ? option->value : NULL;
}

const char *maelys_cli_option_or(
    const maelys_cli_context_t *context, const char *name, const char *fallback) {
    const char *value = maelys_cli_option(context, name);
    return value ? value : fallback;
}

int maelys_cli_flag(const maelys_cli_context_t *context, const char *name) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    return option ? option->boolean_value : 0;
}

int maelys_cli_option_unsigned(
    const maelys_cli_context_t *context, const char *name, uint64_t *out) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    if (!option || !out) return 0;
    *out = option->unsigned_value;
    return 1;
}

int maelys_cli_option_integer(
    const maelys_cli_context_t *context, const char *name, int64_t *out) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    if (!option || !out) return 0;
    *out = option->signed_value;
    return 1;
}

int maelys_cli_option_choice(
    const maelys_cli_context_t *context, const char *name, size_t *out_index) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    if (!option || !out_index) return 0;
    *out_index = option->choice_index;
    return 1;
}

size_t maelys_cli_option_count(
    const maelys_cli_context_t *context, const char *name) {
    return context ? maelys_cli_invocation_option_count(context->invocation, name) : 0u;
}

const char *maelys_cli_option_at(
    const maelys_cli_context_t *context, const char *name, size_t occurrence) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option_at(context->invocation, name, occurrence) : NULL;
    return option ? option->value : NULL;
}

int maelys_cli_json_mode(const maelys_cli_context_t *context) {
    return context && context->invocation &&
        context->invocation->format != MAELYS_CLI_FORMAT_TEXT;
}

int maelys_cli_non_interactive(const maelys_cli_context_t *context) {
    return context && context->invocation && context->invocation->non_interactive;
}

/* ---- rendering --------------------------------------------------------- */

static const char *command_id(const maelys_cli_context_t *context) {
    return context && context->invocation && context->invocation->command ?
        context->invocation->command->id : "unresolved";
}

static int write_json_document(
    maelys_cli_context_t *context, FILE *stream, const char *json) {
    char *formatted = NULL;
    int compact = context->invocation ? context->invocation->compact : 0;
    if (maelys_cli_json_format(json, compact, &formatted) != 0) return -1;
    int failed = fputs(formatted, stream) == EOF || fputc('\n', stream) == EOF;
    free(formatted);
    return failed ? -1 : 0;
}

static int envelope_prefix(
    maelys_cli_json_writer_t *writer, const char *command, int ok,
    int exit_code) {
    return maelys_cli_json_begin_object(writer) == 0 &&
        maelys_cli_json_key_integer(writer, "schemaVersion",
            MAELYS_CLI_SCHEMA_VERSION) == 0 &&
        maelys_cli_json_key_string(writer, "contract", MAELYS_CLI_CONTRACT) == 0 &&
        maelys_cli_json_key_string(writer, "command", command) == 0 &&
        maelys_cli_json_key_boolean(writer, "ok", ok) == 0 &&
        maelys_cli_json_key_integer(writer, "exitCode", exit_code) == 0 ? 0 : -1;
}

int maelys_cli_succeed(
    maelys_cli_context_t *context, const char *data_json, const char *human,
    int exit_code) {
    if (!context || !context->invocation || !context->invocation->command)
        return MAELYS_CLI_EXIT_FAILURE;
    if (context->replied) return exit_code;
    context->replied = 1;
    const char *data = data_json ? data_json : "{}";
    size_t offset = 0u;
    if (maelys_cli_json_validate(data, strlen(data), &offset) != 0) {
        maelys_cli_error_t error;
        context->replied = 0;
        maelys_cli_error_set(&error, MAELYS_CLI_CODE_UNEXPECTED,
            "Report this defect to the command implementation.",
            "Command '%s' produced invalid JSON data at byte %zu.",
            command_id(context), offset);
        return maelys_cli_fail_error(context, &error);
    }
    if (context->invocation->format == MAELYS_CLI_FORMAT_TEXT) {
        if (human) {
            size_t length = strlen(human);
            if (fputs(human, context->out) == EOF ||
                (length && human[length - 1u] != '\n' &&
                 fputc('\n', context->out) == EOF))
                return MAELYS_CLI_EXIT_FAILURE;
        } else if (strcmp(data, "{}") != 0) {
            char *formatted = NULL;
            if (maelys_cli_json_format(data, 0, &formatted) != 0)
                return MAELYS_CLI_EXIT_FAILURE;
            int failed = fputs(formatted, context->out) == EOF ||
                fputc('\n', context->out) == EOF;
            free(formatted);
            if (failed) return MAELYS_CLI_EXIT_FAILURE;
        }
        return fflush(context->out) == 0 ? exit_code : MAELYS_CLI_EXIT_FAILURE;
    }
    if (context->invocation->format == MAELYS_CLI_FORMAT_JSONL) {
        /* Records were already streamed; the process status is the result. */
        return fflush(context->out) == 0 ? exit_code : MAELYS_CLI_EXIT_FAILURE;
    }
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    if (envelope_prefix(&writer, command_id(context), 1, exit_code) != 0 ||
        maelys_cli_json_key_raw(&writer, "data", data) != 0 ||
        maelys_cli_json_end_object(&writer) != 0) {
        maelys_cli_json_writer_clear(&writer);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    char *envelope = maelys_cli_json_finish(&writer);
    if (!envelope) return MAELYS_CLI_EXIT_FAILURE;
    int failed = write_json_document(context, context->out, envelope) != 0;
    free(envelope);
    if (failed || fflush(context->out) != 0) return MAELYS_CLI_EXIT_FAILURE;
    return exit_code;
}

int maelys_cli_succeed_writer(
    maelys_cli_context_t *context, maelys_cli_json_writer_t *data,
    const char *human, int exit_code) {
    char *text = data ? maelys_cli_json_finish(data) : NULL;
    if (!text) {
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED,
            "Report this defect to the command implementation.",
            "Command '%s' could not serialize its data.", command_id(context));
    }
    int result = maelys_cli_succeed(context, text, human, exit_code);
    free(text);
    return result;
}

int maelys_cli_emit_record(
    maelys_cli_context_t *context, const char *record_json,
    const char *human_line) {
    if (!context || !context->invocation || !record_json) return -1;
    if (context->invocation->command->output != MAELYS_CLI_OUTPUT_RECORDS) {
        context->records_failed = 1;
        return -1;
    }
    size_t offset = 0u;
    if (maelys_cli_json_validate(record_json, strlen(record_json), &offset) != 0) {
        context->records_failed = 1;
        return -1;
    }
    context->record_count++;
    switch (context->invocation->format) {
        case MAELYS_CLI_FORMAT_JSONL: {
            char *compact = NULL;
            if (maelys_cli_json_format(record_json, 1, &compact) != 0) return -1;
            int failed = fputs(compact, context->out) == EOF ||
                fputc('\n', context->out) == EOF || fflush(context->out) != 0;
            free(compact);
            return failed ? -1 : 0;
        }
        case MAELYS_CLI_FORMAT_JSON:
            if (context->record_count == 1u &&
                maelys_cli_json_begin_array(&context->records) != 0)
                return -1;
            if (maelys_cli_json_raw(&context->records, record_json) != 0) {
                context->records_failed = 1;
                return -1;
            }
            return 0;
        case MAELYS_CLI_FORMAT_TEXT:
            if (human_line) {
                if (fputs(human_line, context->out) == EOF ||
                    (human_line[0] && human_line[strlen(human_line) - 1u] != '\n' &&
                     fputc('\n', context->out) == EOF))
                    return -1;
                return 0;
            }
            return write_json_document(context, context->out, record_json);
    }
    return -1;
}

int maelys_cli_finish_records(maelys_cli_context_t *context, int exit_code) {
    if (!context || !context->invocation) return MAELYS_CLI_EXIT_FAILURE;
    if (context->records_failed) {
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED,
            "Report this defect to the command implementation.",
            "Command '%s' emitted an invalid record.", command_id(context));
    }
    if (context->invocation->format != MAELYS_CLI_FORMAT_JSON)
        return maelys_cli_succeed(context, "{}", "", exit_code);
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    char *records = NULL;
    if (context->record_count > 0u) {
        if (maelys_cli_json_end_array(&context->records) != 0) {
            maelys_cli_json_writer_clear(&context->records);
            return MAELYS_CLI_EXIT_FAILURE;
        }
        records = maelys_cli_json_finish(&context->records);
        if (!records) return MAELYS_CLI_EXIT_FAILURE;
    }
    int built = maelys_cli_json_begin_object(&data) == 0 &&
        maelys_cli_json_key_unsigned(&data, "count",
            (uint64_t)context->record_count) == 0 &&
        maelys_cli_json_key_raw(&data, "records", records ? records : "[]") == 0 &&
        maelys_cli_json_end_object(&data) == 0;
    free(records);
    if (!built) {
        maelys_cli_json_writer_clear(&data);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    return maelys_cli_succeed_writer(context, &data, NULL, exit_code);
}

static void emit_error(
    maelys_cli_context_t *context, const maelys_cli_error_t *error,
    int exit_code) {
    FILE *err = context && context->err ? context->err : stderr;
    const char *program = context && context->app && context->app->program ?
        context->app->program : "maelys";
    if (context && context->invocation &&
        context->invocation->format != MAELYS_CLI_FORMAT_TEXT) {
        maelys_cli_json_writer_t writer;
        maelys_cli_json_writer_init(&writer);
        if (envelope_prefix(&writer, command_id(context), 0, exit_code) == 0 &&
            maelys_cli_json_key(&writer, "error") == 0 &&
            maelys_cli_json_begin_object(&writer) == 0 &&
            maelys_cli_json_key_string(&writer, "code", error->code) == 0 &&
            maelys_cli_json_key_string(&writer, "message", error->message) == 0 &&
            (!error->hint[0] ||
             maelys_cli_json_key_string(&writer, "hint", error->hint) == 0) &&
            maelys_cli_json_end_object(&writer) == 0 &&
            maelys_cli_json_end_object(&writer) == 0) {
            char *envelope = maelys_cli_json_finish(&writer);
            if (envelope) {
                (void)write_json_document(context, err, envelope);
                free(envelope);
                (void)fflush(err);
                return;
            }
        }
        maelys_cli_json_writer_clear(&writer);
    }
    int color = context ? context->terminal.color_stderr : 0;
    (void)fprintf(err, "%s%s: [%s]%s %s\n",
        maelys_cli_style(color, MAELYS_CLI_STYLE_ERROR), program, error->code,
        maelys_cli_style(color, MAELYS_CLI_STYLE_RESET), error->message);
    if (error->hint[0]) (void)fprintf(err, "Hint: %s\n", error->hint);
    (void)fflush(err);
}

int maelys_cli_fail_error(
    maelys_cli_context_t *context, const maelys_cli_error_t *error) {
    if (!error) return MAELYS_CLI_EXIT_FAILURE;
    if (context) {
        if (context->replied) return MAELYS_CLI_EXIT_FAILURE;
        context->replied = 1;
    }
    emit_error(context, error, MAELYS_CLI_EXIT_FAILURE);
    return MAELYS_CLI_EXIT_FAILURE;
}

int maelys_cli_fail(
    maelys_cli_context_t *context, const char *code, const char *hint,
    const char *format, ...) {
    maelys_cli_error_t error;
    memset(&error, 0, sizeof(error));
    (void)snprintf(error.code, sizeof(error.code), "%s",
        code ? code : MAELYS_CLI_CODE_UNEXPECTED);
    if (hint) (void)snprintf(error.hint, sizeof(error.hint), "%s", hint);
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error.message, sizeof(error.message), format, arguments);
    va_end(arguments);
    return maelys_cli_fail_error(context, &error);
}

int maelys_cli_fail_errno(
    maelys_cli_context_t *context, const char *code, int saved_errno,
    const char *what) {
    maelys_cli_error_t error;
    maelys_cli_error_from_errno(&error, code, saved_errno, what);
    return maelys_cli_fail_error(context, &error);
}

void maelys_cli_warn(maelys_cli_context_t *context, const char *format, ...) {
    FILE *err = context && context->err ? context->err : stderr;
    const char *program = context && context->app ? context->app->program : "maelys";
    int color = context ? context->terminal.color_stderr : 0;
    (void)fprintf(err, "%s%s: warning:%s ",
        maelys_cli_style(color, MAELYS_CLI_STYLE_WARNING), program,
        maelys_cli_style(color, MAELYS_CLI_STYLE_RESET));
    va_list arguments;
    va_start(arguments, format);
    (void)vfprintf(err, format, arguments);
    va_end(arguments);
    (void)fputc('\n', err);
    (void)fflush(err);
}

int maelys_cli_confirm(
    maelys_cli_context_t *context, const char *question, int *out_confirmed) {
    if (!context || !question || !out_confirmed) return -1;
    *out_confirmed = 0;
    if (maelys_cli_non_interactive(context) || !context->terminal.stderr_is_tty) {
        maelys_cli_error_t error;
        maelys_cli_error_set(&error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Supply the explicit option that authorizes this action, or run "
            "interactively.",
            "'%s' needs an interactive confirmation that automation cannot "
            "provide.", command_id(context));
        (void)maelys_cli_fail_error(context, &error);
        return -1;
    }
    (void)fprintf(context->err, "%s [y/N] ", question);
    (void)fflush(context->err);
    char answer[16];
    if (!fgets(answer, sizeof(answer), stdin)) return 0;
    *out_confirmed = (answer[0] == 'y' || answer[0] == 'Y') &&
        (answer[1] == '\n' || answer[1] == '\0' ||
         (answer[1] == 'e' && answer[2] == 's'));
    return 0;
}

/* ---- describe ------------------------------------------------------------ */

static int describe_pattern(
    maelys_cli_json_writer_t *writer, const char *pattern) {
    if (maelys_cli_json_begin_array(writer) != 0) return -1;
    const char *cursor = pattern;
    while (*cursor) {
        const char *end = strchr(cursor, ' ');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (maelys_cli_json_stringn(writer, cursor, length) != 0) return -1;
        if (!end) break;
        cursor = end + 1;
    }
    return maelys_cli_json_end_array(writer);
}

static int describe_option(
    maelys_cli_json_writer_t *writer, const maelys_cli_option_t *option) {
    char long_name[MAELYS_CLI_MAX_OPTION_NAME + 2u];
    (void)snprintf(long_name, sizeof(long_name), "--%s", option->name);
    if (maelys_cli_json_begin_object(writer) != 0 ||
        maelys_cli_json_key_string(writer, "long", long_name) != 0 ||
        maelys_cli_json_key_boolean(writer, "required", option->required) != 0 ||
        maelys_cli_json_key_boolean(writer, "repeatable", option->repeatable) != 0 ||
        maelys_cli_json_key_string(writer, "summary", option->summary) != 0)
        return -1;
    if (option->kind != MAELYS_CLI_VALUE_NONE) {
        if (maelys_cli_json_key(writer, "argument") != 0 ||
            maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "name",
                option->value_name ? option->value_name : "VALUE") != 0 ||
            maelys_cli_json_key_string(writer, "type",
                maelys_cli_value_kind_name(option->kind)) != 0)
            return -1;
        if (option->kind == MAELYS_CLI_VALUE_CHOICE && option->choices) {
            if (maelys_cli_json_key(writer, "choices") != 0 ||
                maelys_cli_json_begin_array(writer) != 0)
                return -1;
            for (size_t i = 0u; option->choices[i]; ++i)
                if (maelys_cli_json_string(writer, option->choices[i]) != 0)
                    return -1;
            if (maelys_cli_json_end_array(writer) != 0) return -1;
        }
        if (option->kind == MAELYS_CLI_VALUE_UNSIGNED ||
            option->kind == MAELYS_CLI_VALUE_SIZE ||
            option->kind == MAELYS_CLI_VALUE_DURATION) {
            if (maelys_cli_json_key_unsigned(writer, "minimum", option->minimum) != 0 ||
                maelys_cli_json_key_unsigned(writer, "maximum",
                    option->maximum ? option->maximum : UINT64_MAX) != 0)
                return -1;
        }
        if (option->kind == MAELYS_CLI_VALUE_INTEGER &&
            (option->signed_minimum != 0 || option->signed_maximum != 0)) {
            if (maelys_cli_json_key_integer(writer, "minimum",
                    option->signed_minimum) != 0 ||
                maelys_cli_json_key_integer(writer, "maximum",
                    option->signed_maximum) != 0)
                return -1;
        }
        if (option->kind == MAELYS_CLI_VALUE_HEX &&
            maelys_cli_json_key_unsigned(writer, "digits",
                (uint64_t)option->hex_digits) != 0)
            return -1;
        if (maelys_cli_json_end_object(writer) != 0) return -1;
    }
    if (option->default_text &&
        maelys_cli_json_key_string(writer, "default", option->default_text) != 0)
        return -1;
    if (maelys_cli_json_key(writer, "requires") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    if (option->requires) {
        char required[MAELYS_CLI_MAX_OPTION_NAME + 2u];
        (void)snprintf(required, sizeof(required), "--%s", option->requires);
        if (maelys_cli_json_string(writer, required) != 0) return -1;
    }
    if (maelys_cli_json_end_array(writer) != 0 ||
        maelys_cli_json_key(writer, "conflictsWith") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    if (option->conflicts_with) {
        char conflict[MAELYS_CLI_MAX_OPTION_NAME + 2u];
        (void)snprintf(conflict, sizeof(conflict), "--%s", option->conflicts_with);
        if (maelys_cli_json_string(writer, conflict) != 0) return -1;
    }
    return maelys_cli_json_end_array(writer) == 0 &&
        maelys_cli_json_end_object(writer) == 0 ? 0 : -1;
}

static int describe_constraints(
    maelys_cli_json_writer_t *writer, const maelys_cli_command_t *command) {
    if (maelys_cli_json_begin_array(writer) != 0) return -1;
    for (size_t i = 0u; i < command->option_count; ++i) {
        const maelys_cli_option_t *option = &command->options[i];
        const char *kinds[2] = {"requires", "at-most-one"};
        const char *targets[2] = {option->requires, option->conflicts_with};
        for (size_t k = 0u; k < 2u; ++k) {
            if (!targets[k]) continue;
            char first[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            char second[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            (void)snprintf(first, sizeof(first), "--%s", option->name);
            (void)snprintf(second, sizeof(second), "--%s", targets[k]);
            if (maelys_cli_json_begin_object(writer) != 0 ||
                maelys_cli_json_key_string(writer, "kind", kinds[k]) != 0 ||
                maelys_cli_json_key(writer, "options") != 0 ||
                maelys_cli_json_begin_array(writer) != 0 ||
                maelys_cli_json_string(writer, first) != 0 ||
                maelys_cli_json_string(writer, second) != 0 ||
                maelys_cli_json_end_array(writer) != 0 ||
                maelys_cli_json_end_object(writer) != 0)
                return -1;
        }
    }
    return maelys_cli_json_end_array(writer);
}

static int describe_command(
    maelys_cli_json_writer_t *writer, const maelys_cli_command_t *command,
    int summary) {
    char synopsis[512];
    if (maelys_cli_command_synopsis(command, synopsis, sizeof(synopsis)) != 0)
        return -1;
    if (maelys_cli_json_begin_object(writer) != 0 ||
        maelys_cli_json_key_string(writer, "id", command->id) != 0 ||
        maelys_cli_json_key(writer, "pattern") != 0 ||
        describe_pattern(writer, command->pattern) != 0 ||
        maelys_cli_json_key_string(writer, "usage", synopsis) != 0 ||
        maelys_cli_json_key_string(writer, "purpose", command->purpose) != 0 ||
        maelys_cli_json_key(writer, "effect") != 0)
        return -1;
    if (command->apply_effect != MAELYS_CLI_EFFECT_NONE) {
        if (maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "plan",
                maelys_cli_effect_name(command->effect)) != 0 ||
            maelys_cli_json_key_string(writer, "apply",
                maelys_cli_effect_name(command->apply_effect)) != 0 ||
            maelys_cli_json_end_object(writer) != 0)
            return -1;
    } else if (maelys_cli_json_string(writer,
                   maelys_cli_effect_name(command->effect)) != 0) {
        return -1;
    }
    if (maelys_cli_json_key_string(writer, "outputMode",
            maelys_cli_output_mode_name(command->output)) != 0 ||
        maelys_cli_json_key_boolean(writer, "external", command->delegate != NULL) != 0 ||
        maelys_cli_json_key_boolean(writer, "hidden", command->hidden) != 0 ||
        maelys_cli_json_key(writer, "input") != 0 ||
        maelys_cli_json_begin_object(writer) != 0 ||
        maelys_cli_json_key_string(writer, "synopsis", synopsis) != 0 ||
        maelys_cli_json_key(writer, "operands") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    for (size_t i = 0u; i < command->operand_count; ++i) {
        const maelys_cli_operand_t *operand = &command->operands[i];
        if (maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "name", operand->name) != 0 ||
            maelys_cli_json_key_boolean(writer, "required", operand->required) != 0 ||
            maelys_cli_json_key_boolean(writer, "variadic", operand->variadic) != 0 ||
            maelys_cli_json_key_string(writer, "summary", operand->summary) != 0 ||
            maelys_cli_json_end_object(writer) != 0)
            return -1;
    }
    if (maelys_cli_json_end_array(writer) != 0 ||
        maelys_cli_json_key(writer, "options") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    for (size_t i = 0u; i < command->option_count; ++i)
        if (describe_option(writer, &command->options[i]) != 0) return -1;
    if (maelys_cli_json_end_array(writer) != 0 ||
        maelys_cli_json_key(writer, "constraints") != 0 ||
        describe_constraints(writer, command) != 0 ||
        maelys_cli_json_key_boolean(writer, "passthrough",
            command->delegate != NULL) != 0 ||
        maelys_cli_json_end_object(writer) != 0)
        return -1;
    if (!summary) {
        if (maelys_cli_json_key_raw(writer, "outputSchema",
                command->output_schema_json ? command->output_schema_json :
                "{\"type\":\"object\"}") != 0 ||
            maelys_cli_json_key(writer, "exitCodes") != 0 ||
            maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "0", "command completed") != 0 ||
            maelys_cli_json_key_string(writer, "1", "execution failed") != 0 ||
            maelys_cli_json_key_string(writer, "2",
                "valid report with violations") != 0 ||
            maelys_cli_json_end_object(writer) != 0)
            return -1;
    }
    return maelys_cli_json_end_object(writer);
}

static int describe_global_options(maelys_cli_json_writer_t *writer) {
    size_t count = 0u;
    const maelys_cli_option_t *options = maelys_cli_transport_options(&count);
    if (maelys_cli_json_begin_array(writer) != 0) return -1;
    for (size_t i = 0u; i < count; ++i)
        if (describe_option(writer, &options[i]) != 0) return -1;
    return maelys_cli_json_end_array(writer);
}

static int describe_data(
    maelys_cli_context_t *context, const char *query, int summary,
    maelys_cli_json_writer_t *writer) {
    const maelys_cli_app_t *app = context->app;
    if (maelys_cli_json_begin_object(writer) != 0 ||
        maelys_cli_json_key_integer(writer, "schemaVersion", 1) != 0 ||
        maelys_cli_json_key_string(writer, "kind",
            query ? "command" : summary ? "summary" : "catalog") != 0 ||
        maelys_cli_json_key_string(writer, "program", app->program) != 0 ||
        maelys_cli_json_key_string(writer, "product", app->product) != 0 ||
        maelys_cli_json_key_string(writer, "version", app->version) != 0 ||
        maelys_cli_json_key_string(writer, "contract", MAELYS_CLI_CONTRACT) != 0 ||
        maelys_cli_json_key_integer(writer, "cliApi", MAELYS_CLI_API) != 0 ||
        maelys_cli_json_key_string(writer, "framework", MAELYS_CLI_VERSION) != 0 ||
        maelys_cli_json_key(writer, "commands") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    size_t count = maelys_cli_app_command_count(app);
    size_t matched = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        if (query && strcmp(query, command->id) != 0) continue;
        if (describe_command(writer, command, summary) != 0) return -1;
        ++matched;
    }
    if (maelys_cli_json_end_array(writer) != 0) return -1;
    if (query && matched == 0u) return 1;
    if (maelys_cli_json_key(writer, "globalOptions") != 0 ||
        describe_global_options(writer) != 0)
        return -1;
    if (!summary) {
        if (maelys_cli_json_key(writer, "output") != 0 ||
            maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "contract", MAELYS_CLI_CONTRACT) != 0 ||
            maelys_cli_json_key_integer(writer, "schemaVersion",
                MAELYS_CLI_SCHEMA_VERSION) != 0 ||
            maelys_cli_json_key_string(writer, "stdout",
                "success data only; protocol streams are explicit exceptions") != 0 ||
            maelys_cli_json_key_string(writer, "stderr",
                "diagnostics and failure envelopes") != 0 ||
            maelys_cli_json_end_object(writer) != 0 ||
            maelys_cli_json_key(writer, "invariants") != 0 ||
            maelys_cli_json_begin_array(writer) != 0 ||
            maelys_cli_json_string(writer,
                "usage and agent discovery share one catalog") != 0 ||
            maelys_cli_json_string(writer,
                "transactional commands plan by default and require --apply") != 0 ||
            maelys_cli_json_string(writer,
                "stdout carries success data; stderr carries failures") != 0 ||
            maelys_cli_json_string(writer,
                "--json and --format json are identical") != 0 ||
            maelys_cli_json_string(writer,
                "unknown or duplicated options are refused") != 0 ||
            maelys_cli_json_string(writer,
                "stream commands reject envelope rendering flags") != 0 ||
            maelys_cli_json_string(writer,
                "external commands receive their arguments verbatim") != 0 ||
            maelys_cli_json_end_array(writer) != 0)
            return -1;
    }
    return maelys_cli_json_end_object(writer) == 0 ? 0 : -1;
}

static int builtin_describe(maelys_cli_context_t *context) {
    const char *query = maelys_cli_operand(context, 0u);
    int summary = maelys_cli_flag(context, "summary");
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    int result = describe_data(context, query, summary, &writer);
    if (result == 1) {
        maelys_cli_json_writer_clear(&writer);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_INVALID_COMMAND,
            "Run describe --summary --format json and select a returned "
            "command identifier.",
            "Unknown command identifier: %s.", query);
    }
    if (result != 0) {
        maelys_cli_json_writer_clear(&writer);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not serialize the catalog.");
    }
    return maelys_cli_succeed_writer(context, &writer, NULL, MAELYS_CLI_EXIT_OK);
}

/* ---- help ---------------------------------------------------------------- */

static void option_help_line(
    FILE *stream, const maelys_cli_option_t *option) {
    char label[256];
    char synopsis_value[160];
    synopsis_value[0] = '\0';
    if (option->kind == MAELYS_CLI_VALUE_CHOICE && option->choices &&
        !option->value_name) {
        size_t used = 0u;
        for (size_t i = 0u; option->choices[i]; ++i) {
            int written = snprintf(synopsis_value + used,
                sizeof(synopsis_value) - used, "%s%s", i ? "|" : " ",
                option->choices[i]);
            if (written < 0 || (size_t)written >= sizeof(synopsis_value) - used)
                break;
            used += (size_t)written;
        }
    } else if (option->kind != MAELYS_CLI_VALUE_NONE) {
        (void)snprintf(synopsis_value, sizeof(synopsis_value), " %s",
            option->value_name ? option->value_name : "VALUE");
    }
    (void)snprintf(label, sizeof(label), "--%s%s%s", option->name,
        synopsis_value, option->repeatable ? " (repeatable)" : "");
    (void)fprintf(stream, "  %-34s %s", label, option->summary);
    if (option->default_text)
        (void)fprintf(stream, " Default: %s.", option->default_text);
    if (option->required) (void)fputs(" Required.", stream);
    if (option->requires) (void)fprintf(stream, " Requires --%s.", option->requires);
    if (option->conflicts_with)
        (void)fprintf(stream, " Conflicts with --%s.", option->conflicts_with);
    (void)fputc('\n', stream);
}

static void command_help_text(
    FILE *stream, const maelys_cli_app_t *app,
    const maelys_cli_command_t *command) {
    char synopsis[512];
    if (maelys_cli_command_synopsis(command, synopsis, sizeof(synopsis)) != 0)
        (void)snprintf(synopsis, sizeof(synopsis), "%s", command->pattern);
    (void)fprintf(stream, "USAGE\n  %s %s\n\n%s\n\nEFFECT\n  ",
        app->program, synopsis, command->purpose);
    if (command->apply_effect != MAELYS_CLI_EFFECT_NONE)
        (void)fprintf(stream, "%s by default; %s with --apply\n",
            maelys_cli_effect_name(command->effect),
            maelys_cli_effect_name(command->apply_effect));
    else
        (void)fprintf(stream, "%s\n", maelys_cli_effect_name(command->effect));
    (void)fprintf(stream, "\nOUTPUT\n  %s%s\n",
        maelys_cli_output_mode_name(command->output),
        command->delegate ? " (arguments are passed to an external program)" : "");
    if (command->operand_count) {
        (void)fputs("\nOPERANDS\n", stream);
        for (size_t i = 0u; i < command->operand_count; ++i) {
            const maelys_cli_operand_t *operand = &command->operands[i];
            (void)fprintf(stream, "  %-34s %s%s\n", operand->name,
                operand->summary, operand->required ? "" : " Optional.");
        }
    }
    if (command->option_count) {
        (void)fputs("\nOPTIONS\n", stream);
        for (size_t i = 0u; i < command->option_count; ++i)
            option_help_line(stream, &command->options[i]);
    }
    (void)fprintf(stream, "\nGLOBAL OPTIONS\n  Run '%s help' for --format, "
        "--json, --compact, --non-interactive and --color.\n", app->program);
}

static void catalog_help_text(FILE *stream, const maelys_cli_app_t *app) {
    (void)fprintf(stream, "%s %s", app->program, app->version);
    if (app->summary && *app->summary) (void)fprintf(stream, " - %s", app->summary);
    (void)fprintf(stream, "\n\nUSAGE\n  %s COMMAND [OPERANDS] [OPTIONS]\n\nCOMMANDS\n",
        app->program);
    size_t count = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        if (command->hidden) continue;
        char synopsis[512];
        if (maelys_cli_command_synopsis(command, synopsis, sizeof(synopsis)) != 0)
            (void)snprintf(synopsis, sizeof(synopsis), "%s", command->pattern);
        if (strlen(synopsis) > 60u)
            (void)fprintf(stream, "  %s\n  %-60s %s\n", synopsis, "",
                command->purpose);
        else
            (void)fprintf(stream, "  %-60s %s\n", synopsis, command->purpose);
    }
    (void)fputs("\nGLOBAL OPTIONS\n", stream);
    size_t transport_count = 0u;
    const maelys_cli_option_t *transport = maelys_cli_transport_options(
        &transport_count);
    for (size_t i = 0u; i < transport_count; ++i)
        option_help_line(stream, &transport[i]);
    (void)fprintf(stream,
        "\nAGENT CONTRACT\n"
        "  Use --format json --non-interactive. Run '%s describe --summary "
        "--format json' first,\n"
        "  then '%s describe COMMAND_ID --format json' for the exact input "
        "and output contract.\n"
        "  Exit 0 is success, 1 is execution failure, and 2 is a completed "
        "validation report with violations.\n"
        "  Transactions plan by default and require --apply. Stream commands "
        "reserve stdout for their protocol.\n"
        "  Success data is written to stdout only; diagnostics and failures "
        "go to stderr.\n", app->program, app->program);
    if (app->agent_guidance && *app->agent_guidance)
        (void)fprintf(stream, "\n%s%s", app->agent_guidance,
            app->agent_guidance[strlen(app->agent_guidance) - 1u] == '\n' ?
            "" : "\n");
}

static int help_for(
    maelys_cli_context_t *context, const maelys_cli_command_t *target) {
    char *text = NULL;
    size_t size = 0u;
    FILE *memory = open_memstream(&text, &size);
    if (!memory) return maelys_cli_fail_errno(context,
        MAELYS_CLI_CODE_UNEXPECTED, errno, "help buffer");
    if (target) command_help_text(memory, context->app, target);
    else catalog_help_text(memory, context->app);
    if (fclose(memory) != 0 || !text) {
        free(text);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not render help.");
    }
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    int built = maelys_cli_json_begin_object(&writer) == 0 &&
        maelys_cli_json_key_string(&writer, "text", text) == 0 &&
        maelys_cli_json_key(&writer, "commands") == 0 &&
        maelys_cli_json_begin_array(&writer) == 0;
    if (built) {
        if (target) built = describe_command(&writer, target, 1) == 0;
        else {
            size_t count = maelys_cli_app_command_count(context->app);
            for (size_t i = 0u; built && i < count; ++i)
                built = describe_command(&writer,
                    maelys_cli_app_command_at(context->app, i), 1) == 0;
        }
    }
    built = built && maelys_cli_json_end_array(&writer) == 0 &&
        maelys_cli_json_end_object(&writer) == 0;
    if (!built) {
        maelys_cli_json_writer_clear(&writer);
        free(text);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not describe the catalog.");
    }
    int result = maelys_cli_succeed_writer(context, &writer, text,
        MAELYS_CLI_EXIT_OK);
    free(text);
    return result;
}

static int builtin_help(maelys_cli_context_t *context) {
    const char *query = maelys_cli_operand(context, 0u);
    const maelys_cli_command_t *target = NULL;
    if (query) {
        target = maelys_cli_app_find_command(context->app, query);
        if (!target) {
            return maelys_cli_fail(context, MAELYS_CLI_CODE_INVALID_COMMAND,
                "Run 'help' without operands to list command identifiers.",
                "Unknown command identifier: %s.", query);
        }
    }
    return help_for(context, target);
}

static int builtin_version(maelys_cli_context_t *context) {
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    int built = maelys_cli_json_begin_object(&writer) == 0 &&
        maelys_cli_json_key_string(&writer, "product", context->app->product) == 0 &&
        maelys_cli_json_key_string(&writer, "program", context->app->program) == 0 &&
        maelys_cli_json_key_string(&writer, "version", context->app->version) == 0 &&
        maelys_cli_json_key_string(&writer, "contract", MAELYS_CLI_CONTRACT) == 0 &&
        maelys_cli_json_key_integer(&writer, "cliApi", MAELYS_CLI_API) == 0 &&
        maelys_cli_json_key_string(&writer, "framework", MAELYS_CLI_VERSION) == 0 &&
        maelys_cli_json_end_object(&writer) == 0;
    if (!built) {
        maelys_cli_json_writer_clear(&writer);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not serialize the version.");
    }
    char human[256];
    (void)snprintf(human, sizeof(human), "%s %s", context->app->program,
        context->app->version);
    return maelys_cli_succeed_writer(context, &writer, human, MAELYS_CLI_EXIT_OK);
}

/* ---- delegation ------------------------------------------------------------ */

static int resolve_delegate(
    maelys_cli_context_t *context, const maelys_cli_command_t *command,
    char *out_path, size_t out_size) {
    const char *delegate = command->delegate;
    if (delegate[0] == '/') {
        const char *explanation = NULL;
        if (maelys_cli_process_check_executable(delegate, &explanation) != 0) {
            (void)maelys_cli_fail(context, MAELYS_CLI_CODE_PROCESS_FAILED,
                "Install the external command with safe ownership and modes.",
                "External command %s is unusable: %s.", delegate,
                explanation ? explanation : strerror(errno));
            return -1;
        }
        if (strlen(delegate) >= out_size) return -1;
        memcpy(out_path, delegate, strlen(delegate) + 1u);
        return 0;
    }
    char executable_directory[PATH_MAX];
    char libexec_program[PATH_MAX];
    char libexec[PATH_MAX];
    const char *directories[3 + 16];
    size_t count = 0u;
    if (maelys_cli_executable_directory(maelys_cli_argv0, executable_directory,
            sizeof(executable_directory)) == 0) {
        directories[count++] = executable_directory;
        int written = snprintf(libexec_program, sizeof(libexec_program),
            "%s/../libexec/%s", executable_directory, context->app->program);
        if (written > 0 && (size_t)written < sizeof(libexec_program))
            directories[count++] = libexec_program;
        written = snprintf(libexec, sizeof(libexec), "%s/../libexec",
            executable_directory);
        if (written > 0 && (size_t)written < sizeof(libexec))
            directories[count++] = libexec;
    }
    for (size_t i = 0u; i < context->app->helper_directory_count && count < 19u; ++i)
        directories[count++] = context->app->helper_directories[i];
    if (maelys_cli_process_resolve(delegate, directories, count, out_path,
            out_size) != 0) {
        (void)maelys_cli_fail(context, MAELYS_CLI_CODE_NOT_FOUND,
            "Install the optional component that provides this command.",
            "External command '%s' for '%s' is not installed.", delegate,
            command->id);
        return -1;
    }
    return 0;
}

static int delegate_command(
    maelys_cli_context_t *context, const maelys_cli_command_t *command) {
    char path[PATH_MAX];
    if (resolve_delegate(context, command, path, sizeof(path)) != 0)
        return MAELYS_CLI_EXIT_FAILURE;
    size_t operand_count = maelys_cli_operand_count(context);
    char **arguments = calloc(operand_count + 2u, sizeof(*arguments));
    if (!arguments)
        return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_UNEXPECTED,
            ENOMEM, "argument vector");
    arguments[0] = path;
    for (size_t i = 0u; i < operand_count; ++i)
        arguments[i + 1u] = (char *)maelys_cli_operand(context, i);
    (void)fflush(context->out);
    (void)fflush(context->err);
    (void)maelys_cli_process_replace(path, arguments, NULL);
    int saved = errno;
    free(arguments);
    return maelys_cli_fail(context, MAELYS_CLI_CODE_PROCESS_FAILED,
        "Verify the external command binary and retry.",
        "Cannot execute %s: %s.", path, strerror(saved));
}

/* ---- entry points ---------------------------------------------------------- */

static void prescan_rendering(
    int argc, char **argv, maelys_cli_invocation_t *invocation) {
    if (!argv) return;
    for (int i = 0; i < argc; ++i) {
        const char *argument = argv[i];
        if (!strcmp(argument, "--json") || !strcmp(argument, "--format=json") ||
            (!strcmp(argument, "--format") && i + 1 < argc &&
             !strcmp(argv[i + 1], "json")))
            invocation->format = MAELYS_CLI_FORMAT_JSON;
        else if (!strcmp(argument, "--format=jsonl") ||
                 (!strcmp(argument, "--format") && i + 1 < argc &&
                  !strcmp(argv[i + 1], "jsonl")))
            invocation->format = MAELYS_CLI_FORMAT_JSONL;
        else if (!strcmp(argument, "--format=text") ||
                 (!strcmp(argument, "--format") && i + 1 < argc &&
                  !strcmp(argv[i + 1], "text")))
            invocation->format = MAELYS_CLI_FORMAT_TEXT;
        if (!strcmp(argument, "--compact") || !strcmp(argument, "--pretty=false"))
            invocation->compact = 1;
        if (!strcmp(argument, "--color=never") ||
            (!strcmp(argument, "--color") && i + 1 < argc &&
             !strcmp(argv[i + 1], "never")))
            invocation->color = MAELYS_CLI_COLOR_NEVER;
    }
}

int maelys_cli_run(
    const maelys_cli_app_t *app, int argc, char **argv, FILE *out, FILE *err) {
    maelys_cli_context_t context;
    maelys_cli_invocation_t invocation;
    maelys_cli_error_t error;
    memset(&context, 0, sizeof(context));
    memset(&invocation, 0, sizeof(invocation));
    context.app = app;
    context.invocation = &invocation;
    context.out = out ? out : stdout;
    context.err = err ? err : stderr;
    context.user_data = app ? app->user_data : NULL;
    maelys_cli_json_writer_init(&context.records);
    if (maelys_cli_catalog_validate(app, &error) != 0) {
        maelys_cli_terminal_detect(&context.terminal, MAELYS_CLI_COLOR_AUTO);
        invocation.command = NULL;
        (void)maelys_cli_fail_error(&context, &error);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    prescan_rendering(argc, argv, &invocation);
    maelys_cli_format_t prescan_format = invocation.format;
    int prescan_compact = invocation.compact;
    maelys_cli_color_mode_t prescan_color = invocation.color;
    if (maelys_cli_parse(app, argc, argv, &invocation, &error) != 0) {
        invocation.format = prescan_format;
        invocation.compact = prescan_compact;
        maelys_cli_terminal_detect(&context.terminal, prescan_color);
        (void)maelys_cli_fail_error(&context, &error);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    maelys_cli_terminal_detect(&context.terminal, invocation.color);
    const maelys_cli_command_t *command = invocation.command;
    int result;
    if (invocation.help_requested && !command->delegate) {
        /* Command-level help renders through the help builtin's contract. */
        result = help_for(&context, command);
    } else if (command->delegate) {
        result = delegate_command(&context, command);
    } else if (!command->handler) {
        result = maelys_cli_fail(&context, MAELYS_CLI_CODE_UNSUPPORTED,
            "Use another command or install the component providing it.",
            "'%s' is not available in this build.", command->id);
    } else {
        result = command->handler(&context);
        if (!context.replied && command->output != MAELYS_CLI_OUTPUT_STREAM) {
            result = maelys_cli_fail(&context, MAELYS_CLI_CODE_UNEXPECTED,
                "Report this defect to the command implementation.",
                "Command '%s' finished without reporting a result.", command->id);
        }
    }
    maelys_cli_json_writer_clear(&context.records);
    (void)fflush(context.out);
    (void)fflush(context.err);
    return result;
}

int maelys_cli_main(const maelys_cli_app_t *app, int argc, char **argv) {
    if (argc > 0 && argv && argv[0]) maelys_cli_argv0 = argv[0];
    return maelys_cli_run(app, argc > 0 ? argc - 1 : 0,
        argc > 0 ? argv + 1 : NULL, stdout, stderr);
}
