#include "maelys/cli/app.h"
#include "maelys/cli/files.h"
#include "maelys/cli/process.h"
#include "maelys/cli/values.h"
#include "maelys/cli/version.h"
#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const char *maelys_cli_argv0 = NULL;

/* ---- built-in commands ------------------------------------------------ */

static int builtin_help(maelys_cli_context_t *context);
static int builtin_version(maelys_cli_context_t *context);
static int builtin_describe(maelys_cli_context_t *context);
static int builtin_completion(maelys_cli_context_t *context);
static int schema_required(
    const maelys_cli_command_t *command, const char **out_array, size_t *out_length);
static int schema_requires(const maelys_cli_command_t *command, const char *name);
static int builtin_complete(maelys_cli_context_t *context);

static const char *const shell_choices[] = {"bash", "zsh", "fish", NULL};
static const maelys_cli_operand_t completion_operands[] = {
    {MAELYS_CLI_OPERAND_CHOICE("SHELL",
     "Shell whose completion script is printed.", shell_choices)},
};
static const maelys_cli_operand_t complete_operands[] = {
    {MAELYS_CLI_OPERAND_REST("WORDS",
     "Command line words after the program name; the last one is the "
     "prefix being completed.")},
};

static const maelys_cli_operand_t help_operands[] = {
    {MAELYS_CLI_OPERAND_OPTIONAL("COMMAND_ID",
     "Stable command identifier whose help is shown, the family of "
     "commands under an identifier, or `conventions`.")},
};
static const maelys_cli_operand_t describe_operands[] = {
    {MAELYS_CLI_OPERAND_OPTIONAL("COMMAND_ID",
     "Stable command identifier returned by the catalog.")},
};
static const maelys_cli_option_t describe_options[] = {
    {MAELYS_CLI_FLAG("summary",
     "Return the compact command inventory without output schemas.")},
    {MAELYS_CLI_STRING("prefix", "PREFIX",
     "Restrict the summary to one command namespace: the command named "
     "PREFIX and every command whose identifier starts with PREFIX followed "
     "by a dot."), .depends_on = "summary", .conflicts_with = "COMMAND_ID",
     .pattern = "^[a-z]([a-z0-9.-]*[a-z0-9-])?$"},
};

#define HELP_SCHEMA "{\"type\":\"object\",\"additionalProperties\":false," \
    "\"required\":[\"text\",\"commands\"],\"properties\":{\"text\":{\"type\":" \
    "\"string\"},\"commands\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}}}"
#define VERSION_SCHEMA "{\"type\":\"object\",\"additionalProperties\":false," \
    "\"required\":[\"product\",\"program\",\"version\",\"contract\",\"cliApi\"," \
    "\"framework\"],\"properties\":{\"product\":{\"type\":\"string\"}," \
    "\"program\":{\"type\":\"string\"},\"version\":{\"type\":\"string\"}," \
    "\"contract\":{\"const\":\"agent-cli/v2\"},\"cliApi\":{\"type\":\"integer\"}," \
    "\"framework\":{\"type\":\"string\"}}}"
#define DESCRIBE_SCHEMA "{\"type\":\"object\",\"description\":\"Command " \
    "catalog, summary or single descriptor.\",\"additionalProperties\":true," \
    "\"required\":[\"schemaVersion\",\"kind\",\"program\",\"commands\"]}"
#define COMPLETION_SCHEMA "{\"type\":\"object\",\"additionalProperties\":" \
    "false,\"required\":[\"shell\",\"script\"],\"properties\":{\"shell\":{" \
    "\"enum\":[\"bash\",\"zsh\",\"fish\"]},\"script\":{\"type\":\"string\"}}}"
#define COMPLETE_SCHEMA "{\"type\":\"object\",\"additionalProperties\":false," \
    "\"required\":[\"count\",\"records\"],\"properties\":{\"count\":{\"type\":" \
    "\"integer\"},\"records\":{\"type\":\"array\",\"items\":{\"type\":\"object\"," \
    "\"required\":[\"word\"]}}}}"

static const maelys_cli_command_t builtins[] = {
    {MAELYS_CLI_READ("help", "help",
     "Show the generated CLI guide or one command's help.", builtin_help),
     MAELYS_CLI_OPERANDS(help_operands), MAELYS_CLI_SCHEMA(HELP_SCHEMA),
     .synopsis = "help [COMMAND_ID] | --help"},
    {MAELYS_CLI_READ("version", "version", "Return product identity.",
     builtin_version), MAELYS_CLI_SCHEMA(VERSION_SCHEMA),
     .synopsis = "version | --version"},
    {MAELYS_CLI_READ("describe", "describe",
     "Return the machine-readable catalog, summary or one descriptor.",
     builtin_describe), MAELYS_CLI_OPERANDS(describe_operands),
     MAELYS_CLI_OPTIONS(describe_options), MAELYS_CLI_SCHEMA(DESCRIBE_SCHEMA)},
    {MAELYS_CLI_READ("completion", "completion",
     "Print the shell completion script generated from the catalog.",
     builtin_completion), MAELYS_CLI_OPERANDS(completion_operands),
     MAELYS_CLI_SCHEMA(COMPLETION_SCHEMA)},
    {MAELYS_CLI_RECORDS("complete.candidates", "__complete",
     "Return completion candidates for a partial command line.",
     builtin_complete), MAELYS_CLI_OPERANDS(complete_operands),
     MAELYS_CLI_SCHEMA(COMPLETE_SCHEMA), .hidden = 1},
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
              *p == '-' || *p == '_' || *p == ' '))
            return 0;
        if (*p == ' ' && p[1] == ' ') return 0;
    }
    return 1;
}

/* The eleven stable codes of the contract, so a catalog cannot invent one.
 * An unavailable command names the code that fits its cause; anything else
 * would reach an agent as a code it cannot act on. */
static int code_is_stable(const char *code) {
    static const char *const codes[] = {
        MAELYS_CLI_CODE_INVALID_COMMAND, MAELYS_CLI_CODE_VALIDATION_FAILED,
        MAELYS_CLI_CODE_PRECONDITION_FAILED, MAELYS_CLI_CODE_POLICY_FAILED,
        MAELYS_CLI_CODE_ACCESS_DENIED, MAELYS_CLI_CODE_NOT_FOUND,
        MAELYS_CLI_CODE_IO_FAILED, MAELYS_CLI_CODE_PROCESS_FAILED,
        MAELYS_CLI_CODE_PROTOCOL_FAILED, MAELYS_CLI_CODE_UNSUPPORTED,
        MAELYS_CLI_CODE_UNEXPECTED,
    };
    for (size_t i = 0u; i < sizeof(codes) / sizeof(codes[0]); ++i)
        if (!strcmp(code, codes[i])) return 1;
    return 0;
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
    if (command->apply_effect != MAELYS_CLI_EFFECT_NONE) {
        /* On a transaction --expect has one meaning and one shape
         * (agent-cli/v2 2.9, section 4): MAELYS_CLI_EXPECT_OPTION, and a
         * fingerprint the output schema requires. */
        for (size_t i = 0u; i < command->option_count; ++i) {
            const maelys_cli_option_t *option = &command->options[i];
            if (strcmp(option->name, "expect") != 0) continue;
            int shaped = option->kind == MAELYS_CLI_VALUE_DIGEST &&
                option->choices && option->choices[0] &&
                !strcmp(option->choices[0], "sha256") && !option->choices[1] &&
                option->depends_on && !strcmp(option->depends_on, "apply") &&
                !option->repeatable && !option->required && !option->hidden;
            if (!shaped) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Transactional command '%s' declares --expect, which is "
                    "reserved for binding a plan: declare it with "
                    "MAELYS_CLI_EXPECT_OPTION, or name the option otherwise.", id);
                return -1;
            }
            if (!schema_requires(command, "fingerprint")) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Transactional command '%s' declares --expect: its output "
                    "schema must list \"fingerprint\" in its top-level "
                    "\"required\".", id);
                return -1;
            }
        }
    }
    if (command->output > MAELYS_CLI_OUTPUT_STREAM) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' has an invalid output mode.", id);
        return -1;
    }
    if (command->protocol && (!*command->protocol ||
        command->output != MAELYS_CLI_OUTPUT_STREAM)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' names a protocol but is not a stream "
            "command.", id);
        return -1;
    }
    if (command->unavailable) {
        if (!*command->unavailable || command->handler || command->delegate) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' marked unavailable must name a reason "
                "and have neither handler nor delegate.", id);
            return -1;
        }
        if (command->unavailable_code &&
            !code_is_stable(command->unavailable_code)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' names '%s' as its unavailable code, "
                "which is not one of the stable codes.", id,
                command->unavailable_code);
            return -1;
        }
    } else if (command->unavailable_code) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' names an unavailable code without an "
            "'unavailable' reason.", id);
        return -1;
    } else if ((command->handler == NULL) == (command->delegate == NULL)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
            "Catalog command '%s' needs exactly one of handler or delegate, "
            "or an 'unavailable' reason.", id);
        return -1;
    }
    char *synopsis = maelys_cli_command_synopsis_alloc(command);
    if (!synopsis) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED,
            "Shorten operands, options or their placeholders.",
            "Catalog command '%s' has a synopsis longer than %u bytes.", id,
            MAELYS_CLI_MAX_SYNOPSIS);
        return -1;
    }
    free(synopsis);
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
        /* As for an option's pattern (spec 2.6: an operand describes its
         * value exactly as an argument does). */
        if (operand->pattern && operand->kind != MAELYS_CLI_VALUE_STRING &&
            operand->kind != MAELYS_CLI_VALUE_PATH) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' operand %s declares a pattern on a "
                "kind that is not string or path.", id, operand->name);
            return -1;
        }
        if (operand->pattern) {
            regex_t compiled;
            int compiled_result = maelys_cli_pattern_compile(operand->pattern,
                &compiled);
            if (compiled_result == 0) regfree(&compiled);
            else {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' operand %s declares a pattern that "
                    "is not a valid extended regular expression.", id,
                    operand->name);
                return -1;
            }
        }
        if (operand->kind > MAELYS_CLI_VALUE_DIGEST ||
            ((operand->kind == MAELYS_CLI_VALUE_CHOICE ||
              operand->kind == MAELYS_CLI_VALUE_DIGEST) &&
             (!operand->choices || !operand->choices[0])) ||
            (operand->kind == MAELYS_CLI_VALUE_HEX && operand->hex_digits == 0u) ||
            (operand->maximum && operand->minimum > operand->maximum) ||
            operand->signed_minimum > operand->signed_maximum) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' operand %s has an invalid type "
                "declaration.", id, operand->name);
            return -1;
        }
        for (size_t a = 0u; operand->kind == MAELYS_CLI_VALUE_DIGEST &&
             operand->choices[a]; ++a) {
            if (maelys_cli_digest_hex_digits(operand->choices[a]) == 0u) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' operand %s names unknown digest "
                    "algorithm '%s'.", id, operand->name, operand->choices[a]);
                return -1;
            }
        }
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
        if (option->kind > MAELYS_CLI_VALUE_DIGEST ||
            (option->kind == MAELYS_CLI_VALUE_DIGEST &&
             (!option->choices || !option->choices[0])) ||
            (option->kind == MAELYS_CLI_VALUE_CHOICE &&
             (!option->choices || !option->choices[0])) ||
            (option->kind == MAELYS_CLI_VALUE_HEX && option->hex_digits == 0u) ||
            (option->kind == MAELYS_CLI_VALUE_HEX &&
             option->hex_digits_alternative == option->hex_digits) ||
            (option->kind != MAELYS_CLI_VALUE_HEX &&
             option->hex_digits_alternative != 0u) ||
            (option->maximum && option->minimum > option->maximum) ||
            option->signed_minimum > option->signed_maximum) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' option --%s has an invalid value "
                "declaration.", id, option->name);
            return -1;
        }
        for (size_t a = 0u; option->kind == MAELYS_CLI_VALUE_DIGEST &&
             option->choices[a]; ++a) {
            if (maelys_cli_digest_hex_digits(option->choices[a]) == 0u) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s names unknown digest "
                    "algorithm '%s'.", id, option->name, option->choices[a]);
                return -1;
            }
        }
        if (option->pattern && option->kind != MAELYS_CLI_VALUE_STRING &&
            option->kind != MAELYS_CLI_VALUE_PATH) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' option --%s declares a pattern on a "
                "kind that is not string or path.", id, option->name);
            return -1;
        }
        if (option->pattern) {
            regex_t compiled;
            int compiled_result = maelys_cli_pattern_compile(option->pattern,
                &compiled);
            if (compiled_result == 0) regfree(&compiled);
            else {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s declares a pattern that "
                    "is not a valid extended regular expression.", id,
                    option->name);
                return -1;
            }
        }
        if (option->hidden && option->required) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' option --%s is hidden and required; a "
                "required option must be shown.", id, option->name);
            return -1;
        }
        if (option->default_text) {
            maelys_cli_parsed_option_t probe;
            maelys_cli_error_t detail;
            memset(&probe, 0, sizeof(probe));
            if (option->kind == MAELYS_CLI_VALUE_NONE ||
                maelys_cli_option_validate_text(option, option->default_text,
                    &probe, &detail) != 0) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s has a default '%s' that "
                    "its own kind refuses.", id, option->name, option->default_text);
                return -1;
            }
        }
        for (size_t d = 0u; option->depends_on_all && option->depends_on_all[d]; ++d) {
            int found = 0;
            for (size_t j = 0u; j < command->option_count; ++j)
                if (j != i && !strcmp(command->options[j].name, option->depends_on_all[d]))
                    found = 1;
            if (!found) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s references unknown "
                    "option --%s.", id, option->name, option->depends_on_all[d]);
                return -1;
            }
        }
        if (option->group) {
            size_t members = 0u;
            for (size_t j = 0u; j < command->option_count; ++j)
                if (command->options[j].group &&
                    !strcmp(command->options[j].group, option->group))
                    ++members;
            if (!*option->group || members < 2u) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s names group '%s' with "
                    "fewer than two members.", id, option->name, option->group);
                return -1;
            }
        }
        const char *references[2] = {option->depends_on, option->conflicts_with};
        for (size_t r = 0u; r < 2u; ++r) {
            if (!references[r]) continue;
            int found = 0;
            for (size_t j = 0u; j < command->option_count; ++j)
                if (j != i && !strcmp(command->options[j].name, references[r]))
                    found = 1;
            /* conflicts_with may also name an operand of the command. */
            for (size_t j = 0u; r == 1u && j < command->operand_count; ++j)
                if (!strcmp(command->operands[j].name, references[r]))
                    found = 1;
            if (!found) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' option --%s references unknown "
                    "option --%s.", id, option->name, references[r]);
                return -1;
            }
        }
    }
    for (size_t c = 0u; c < command->constraint_count; ++c) {
        const maelys_cli_constraint_t *rule = &command->constraints[c];
        if (rule->kind == MAELYS_CLI_CONSTRAINT_ALL_OR_NONE) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' states an all-or-none constraint; "
                "declare .group on its options instead, the one form the "
                "entry is derived from.", id);
            return -1;
        }
        if ((unsigned)rule->kind > MAELYS_CLI_CONSTRAINT_ALL_OR_NONE) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' constraint %zu has an unknown kind.",
                id, c);
            return -1;
        }
        size_t listed = 0u;
        for (size_t o = 0u; rule->options && rule->options[o]; ++o, ++listed) {
            int found = 0;
            for (size_t j = 0u; j < command->option_count; ++j)
                if (!strcmp(command->options[j].name, rule->options[o]))
                    found = 1;
            if (!found) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                    "Catalog command '%s' constraint %zu names unknown option "
                    "--%s.", id, c, rule->options[o]);
                return -1;
            }
            for (size_t p = 0u; p < o; ++p)
                if (!strcmp(rule->options[p], rule->options[o])) {
                    maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                        "Catalog command '%s' constraint %zu names --%s "
                        "twice.", id, c, rule->options[o]);
                    return -1;
                }
        }
        if (listed < 2u) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, hint,
                "Catalog command '%s' constraint %zu names fewer than two "
                "options.", id, c);
            return -1;
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

int maelys_cli_operand_unsigned(
    const maelys_cli_context_t *context, size_t index, uint64_t *out) {
    const maelys_cli_parsed_option_t *value = context ?
        maelys_cli_invocation_operand_value(context->invocation, index) : NULL;
    if (!value || !out) return 0;
    *out = value->unsigned_value;
    return 1;
}

int maelys_cli_operand_integer(
    const maelys_cli_context_t *context, size_t index, int64_t *out) {
    const maelys_cli_parsed_option_t *value = context ?
        maelys_cli_invocation_operand_value(context->invocation, index) : NULL;
    if (!value || !out) return 0;
    *out = value->signed_value;
    return 1;
}

int maelys_cli_operand_choice(
    const maelys_cli_context_t *context, size_t index, size_t *out_index) {
    const maelys_cli_parsed_option_t *value = context ?
        maelys_cli_invocation_operand_value(context->invocation, index) : NULL;
    if (!value || !out_index) return 0;
    *out_index = value->choice_index;
    return 1;
}

const char *maelys_cli_option(const maelys_cli_context_t *context, const char *name) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    return option ? option->value : NULL;
}

static const maelys_cli_option_t *declared_option(
    const maelys_cli_context_t *context, const char *name) {
    if (!context || !context->invocation || !context->invocation->command || !name)
        return NULL;
    const maelys_cli_command_t *command = context->invocation->command;
    for (size_t i = 0u; i < command->option_count; ++i)
        if (!strcmp(command->options[i].name, name)) return &command->options[i];
    return NULL;
}

/* Typed value from the invocation, else from the validated default. */
static int typed_option(
    const maelys_cli_context_t *context, const char *name,
    maelys_cli_parsed_option_t *out) {
    const maelys_cli_parsed_option_t *given = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    if (given) {
        *out = *given;
        return 1;
    }
    const maelys_cli_option_t *option = declared_option(context, name);
    if (!option || !option->default_text) return 0;
    maelys_cli_error_t ignored;
    memset(out, 0, sizeof(*out));
    return maelys_cli_option_validate_text(option, option->default_text, out,
        &ignored) == 0;
}

const char *maelys_cli_option_or(
    const maelys_cli_context_t *context, const char *name, const char *fallback) {
    const char *value = maelys_cli_option(context, name);
    if (value) return value;
    const maelys_cli_option_t *option = declared_option(context, name);
    return option && option->default_text ? option->default_text : fallback;
}

int maelys_cli_flag(const maelys_cli_context_t *context, const char *name) {
    const maelys_cli_parsed_option_t *option = context ?
        maelys_cli_invocation_option(context->invocation, name) : NULL;
    return option ? option->boolean_value : 0;
}

int maelys_cli_option_unsigned(
    const maelys_cli_context_t *context, const char *name, uint64_t *out) {
    maelys_cli_parsed_option_t value;
    if (!out || !typed_option(context, name, &value)) return 0;
    *out = value.unsigned_value;
    return 1;
}

int maelys_cli_option_integer(
    const maelys_cli_context_t *context, const char *name, int64_t *out) {
    maelys_cli_parsed_option_t value;
    if (!out || !typed_option(context, name, &value)) return 0;
    *out = value.signed_value;
    return 1;
}

int maelys_cli_option_choice(
    const maelys_cli_context_t *context, const char *name, size_t *out_index) {
    maelys_cli_parsed_option_t value;
    if (!out_index || !typed_option(context, name, &value)) return 0;
    *out_index = value.choice_index;
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

int maelys_cli_replied(const maelys_cli_context_t *context) {
    return context ? context->replied : 0;
}

/* ---- trunk diagnostics (spec 2.3, section 5) ---------------------------- */

int maelys_cli_verbose(const maelys_cli_context_t *context) {
    return context && context->invocation && context->invocation->verbose &&
        context->invocation->format == MAELYS_CLI_FORMAT_TEXT;
}

int maelys_cli_progress_wanted(const maelys_cli_context_t *context) {
    if (!context || !context->invocation ||
        context->invocation->format != MAELYS_CLI_FORMAT_TEXT)
        return 0;
    if (context->invocation->progress == MAELYS_CLI_ALWAYS) return 1;
    return context->invocation->progress == MAELYS_CLI_AUTO &&
        context->terminal.stderr_is_tty;
}

void maelys_cli_progress_done(maelys_cli_context_t *context) {
    if (!context || !context->progress_shown) return;
    FILE *err = context->err ? context->err : stderr;
    (void)fputs("\r\033[K", err);
    (void)fflush(err);
    context->progress_shown = 0;
}

void maelys_cli_progress(maelys_cli_context_t *context, const char *format, ...) {
    if (!format || !maelys_cli_progress_wanted(context)) return;
    FILE *err = context->err ? context->err : stderr;
    char message[MAELYS_CLI_MAX_ERROR_MESSAGE];
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (context->terminal.stderr_is_tty) {
        /* Rewritten in place and erased by maelys_cli_progress_done(). */
        (void)fputc('\r', err);
        maelys_cli_fprint_terminal_safe(err, message);
        (void)fputs("\033[K", err);
        context->progress_shown = 1;
    } else {
        maelys_cli_fprint_terminal_safe(err, message);
        (void)fputc('\n', err);
    }
    (void)fflush(err);
}

void maelys_cli_detail(maelys_cli_context_t *context, const char *format, ...) {
    if (!format || !maelys_cli_verbose(context)) return;
    maelys_cli_progress_done(context);
    FILE *err = context->err ? context->err : stderr;
    const char *program = context->app ? context->app->program : "maelys";
    char message[MAELYS_CLI_MAX_ERROR_MESSAGE];
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    (void)fprintf(err, "%s: ", program);
    maelys_cli_fprint_terminal_safe(err, message);
    (void)fputc('\n', err);
    (void)fflush(err);
}

/* ---- pager (spec 2.3, section 5) ---------------------------------------- */

/* Splits PAGER with POSIX shell quoting and backslash rules, without any
 * expansion. Returns the word count, or -1 on an unterminated quote. */
static int split_posix_words(
    const char *text, char *buffer, size_t buffer_size, char **words,
    size_t max_words) {
    size_t used = 0u;
    int count = 0;
    const char *cursor = text;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n') ++cursor;
        if (!*cursor) break;
        if ((size_t)count + 1u >= max_words) return -1;
        words[count] = buffer + used;
        int quote = 0;
        for (; *cursor; ++cursor) {
            char c = *cursor;
            if (quote == '\'') {
                if (c == '\'') { quote = 0; continue; }
            } else if (quote == '"') {
                if (c == '"') { quote = 0; continue; }
                if (c == '\\' && cursor[1] && strchr("\"\\$`\n", cursor[1])) {
                    c = *++cursor;
                    if (c == '\n') continue;
                }
            } else {
                if (c == ' ' || c == '\t' || c == '\n') break;
                if (c == '\'' || c == '"') { quote = c; continue; }
                if (c == '\\') {
                    if (!cursor[1]) break;
                    c = *++cursor;
                    if (c == '\n') continue;
                }
            }
            if (used + 2u >= buffer_size) return -1;
            buffer[used++] = c;
        }
        if (quote) return -1;
        buffer[used++] = '\0';
        ++count;
    }
    words[count] = NULL;
    return count;
}

static int pager_applies(const maelys_cli_context_t *context) {
    const maelys_cli_invocation_t *invocation = context->invocation;
    const maelys_cli_command_t *command = invocation->command;
    if (invocation->format != MAELYS_CLI_FORMAT_TEXT) return 0;
    if (invocation->non_interactive || invocation->pager == MAELYS_CLI_NEVER)
        return 0;
    if (!command || command->delegate || command->output == MAELYS_CLI_OUTPUT_STREAM)
        return 0;
    /* Only the process's own terminal stdout is paged, never a test stream. */
    if (context->out != stdout || !context->terminal.stdout_is_tty) return 0;
    return 1;
}

/* Starts the pager named by PAGER (or less with LESS=FRX) and routes the
 * text rendering through it; on any failure the rendering stays on stdout.
 * The pager is the one program this library resolves through PATH: it is
 * the user's own choice, started only when stdout is that user's terminal. */
static void start_pager(maelys_cli_context_t *context) {
    const char *setting = getenv("PAGER");
    char buffer[4096];
    char *words[64];
    if (setting) {
        int count = split_posix_words(setting, buffer, sizeof(buffer), words,
            sizeof(words) / sizeof(words[0]));
        if (count <= 0) return; /* empty disables; malformed falls back */
    } else {
        words[0] = (char *)"less";
        words[1] = NULL;
    }
    int data_pipe[2];
    int error_pipe[2];
    if (pipe(data_pipe) != 0) return;
    if (pipe(error_pipe) != 0) {
        (void)close(data_pipe[0]);
        (void)close(data_pipe[1]);
        return;
    }
    (void)fcntl(error_pipe[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC);
    (void)fcntl(data_pipe[1], F_SETFD, FD_CLOEXEC);
    (void)fflush(stdout);
    pid_t child = fork();
    if (child < 0) {
        (void)close(data_pipe[0]);
        (void)close(data_pipe[1]);
        (void)close(error_pipe[0]);
        (void)close(error_pipe[1]);
        return;
    }
    if (child == 0) {
        (void)close(error_pipe[0]);
        if (dup2(data_pipe[0], STDIN_FILENO) < 0) _exit(127);
        (void)close(data_pipe[0]);
        if (!setting && !getenv("LESS")) (void)setenv("LESS", "FRX", 1);
        execvp(words[0], words);
        int saved = errno;
        (void)!write(error_pipe[1], &saved, sizeof(saved));
        _exit(127);
    }
    (void)close(data_pipe[0]);
    (void)close(error_pipe[1]);
    int failure = 0;
    ssize_t amount;
    do {
        amount = read(error_pipe[0], &failure, sizeof(failure));
    } while (amount < 0 && errno == EINTR);
    (void)close(error_pipe[0]);
    if (amount != 0) {
        /* The pager could not start: reap it and render on stdout. */
        (void)close(data_pipe[1]);
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        return;
    }
    FILE *stream = fdopen(data_pipe[1], "w");
    if (!stream) {
        (void)close(data_pipe[1]);
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        return;
    }
    context->pager_out = context->out;
    context->out = stream;
    context->pager_pid = (int)child;
    (void)signal(SIGPIPE, SIG_IGN); /* a pager quit early ends the rendering */
}

static void finish_pager(maelys_cli_context_t *context) {
    if (!context->pager_pid) return;
    (void)fflush(context->out);
    (void)fclose(context->out);
    context->out = context->pager_out;
    context->pager_out = NULL;
    int status;
    while (waitpid((pid_t)context->pager_pid, &status, 0) < 0 && errno == EINTR) {}
    context->pager_pid = 0;
    (void)signal(SIGPIPE, SIG_DFL);
}

/* ---- rendering --------------------------------------------------------- */

static const char *command_id(const maelys_cli_context_t *context) {
    return context && context->invocation && context->invocation->command ?
        context->invocation->command->id : "unknown";
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

/* Appends pre-serialized JSON to a writer without validating it. */
static int append_trusted(maelys_cli_json_writer_t *writer, const char *json) {
    size_t length = strlen(json);
    if (writer->failed) return -1;
    if (length > SIZE_MAX - writer->size - 1u) return -1;
    size_t needed = writer->size + length + 1u;
    if (needed > writer->capacity) {
        size_t capacity = writer->capacity ? writer->capacity : 256u;
        while (capacity < needed) capacity *= 2u;
        char *grown = realloc(writer->data, capacity);
        if (!grown) {
            writer->failed = 1;
            return -1;
        }
        writer->data = grown;
        writer->capacity = capacity;
    }
    memcpy(writer->data + writer->size, json, length);
    writer->size += length;
    writer->data[writer->size] = '\0';
    writer->pending_key = 0;
    return 0;
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

/* Defined below, with the text-rendering primitives it reuses (spec 2.4). */
static int reply_field(
    maelys_cli_context_t *context, const char *data, int exit_code);

static int succeed_with(
    maelys_cli_context_t *context, const char *data_json, const char *human,
    int exit_code, int trusted) {
    if (!context || !context->invocation || !context->invocation->command)
        return MAELYS_CLI_EXIT_FAILURE;
    if (context->replied) return exit_code;
    if (!context->expect_checked && context->invocation->command->apply_effect !=
            MAELYS_CLI_EFFECT_NONE &&
        maelys_cli_invocation_option(context->invocation, "expect")) {
        /* --expect was given and the handler answers without having asked
         * maelys_cli_expect(): the caller would believe a binding nobody
         * checked. Whatever was done is not known to be the reviewed plan. */
        maelys_cli_error_t error;
        maelys_cli_error_set(&error, MAELYS_CLI_CODE_UNEXPECTED,
            "Report this defect to the command implementation.",
            "Command '%s' answered without checking --expect: what it did is "
            "not known to be the plan the fingerprint names.",
            command_id(context));
        return maelys_cli_fail_error(context, &error);
    }
    context->replied = 1;
    const char *data = data_json ? data_json : "{}";
    size_t offset = 0u;
    if (!trusted && maelys_cli_json_validate(data, strlen(data), &offset) != 0) {
        maelys_cli_error_t error;
        context->replied = 0;
        maelys_cli_error_set(&error, MAELYS_CLI_CODE_UNEXPECTED,
            "Report this defect to the command implementation.",
            "Command '%s' produced invalid JSON data at byte %zu.",
            command_id(context), offset);
        return maelys_cli_fail_error(context, &error);
    }
    if (context->invocation->field)
        return reply_field(context, data, exit_code);
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
        /* The records were written by maelys_cli_finish_records(); the
         * process status is the result. */
        return fflush(context->out) == 0 ? exit_code : MAELYS_CLI_EXIT_FAILURE;
    }
    if (trusted && context->invocation->compact) {
        /* Verbatim splice: prefix, data, suffix. No validation, no pass. */
        maelys_cli_json_writer_t writer;
        maelys_cli_json_writer_init(&writer);
        if (envelope_prefix(&writer, command_id(context), 1, exit_code) != 0) {
            maelys_cli_json_writer_clear(&writer);
            return MAELYS_CLI_EXIT_FAILURE;
        }
        char *prefix = writer.data; /* open object without its closing brace */
        int failed = fputs(prefix, context->out) == EOF ||
            fputs(",\"data\":", context->out) == EOF ||
            fputs(data, context->out) == EOF ||
            fputs("}\n", context->out) == EOF;
        maelys_cli_json_writer_clear(&writer);
        if (failed || fflush(context->out) != 0) return MAELYS_CLI_EXIT_FAILURE;
        return exit_code;
    }
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    if (envelope_prefix(&writer, command_id(context), 1, exit_code) != 0 ||
        (trusted ? (maelys_cli_json_key(&writer, "data") == 0 &&
                    append_trusted(&writer, data) == 0 ? 0 : -1) :
                   maelys_cli_json_key_raw(&writer, "data", data)) != 0 ||
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

int maelys_cli_succeed(
    maelys_cli_context_t *context, const char *data_json, const char *human,
    int exit_code) {
    return succeed_with(context, data_json, human, exit_code, 0);
}

int maelys_cli_succeed_trusted(
    maelys_cli_context_t *context, const char *data_json, const char *human,
    int exit_code) {
    return succeed_with(context, data_json, human, exit_code, 1);
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

static int emit_record_with(
    maelys_cli_context_t *context, const char *record_json,
    const char *human_line, int trusted) {
    if (!context || !context->invocation || !record_json) return -1;
    if (context->invocation->command->output != MAELYS_CLI_OUTPUT_RECORDS) {
        context->records_failed = 1;
        return -1;
    }
    size_t offset = 0u;
    if (!trusted &&
        maelys_cli_json_validate(record_json, strlen(record_json), &offset) != 0) {
        context->records_failed = 1;
        return -1;
    }
    context->record_count++;
    /* --field defers everything to maelys_cli_finish_records(): count and
     * records must exist as named members of one object before the field
     * renderer can select between them, so neither the terminal human line
     * nor immediate jsonl streaming can run; every format buffers, exactly
     * as the JSON format always has. */
    if (context->invocation->format == MAELYS_CLI_FORMAT_TEXT &&
        !context->invocation->field) {
        /* On a terminal the human line is shown as given; elsewhere, or
         * without one, records are held for the tabular pipe form of
         * spec 2.3 section 7, rendered by maelys_cli_finish_records(). */
        int tty = context->pager_pid ? context->terminal.stdout_is_tty :
            (context->out && fileno(context->out) >= 0 &&
             isatty(fileno(context->out)));
        if (tty && human_line) {
            if (fputs(human_line, context->out) == EOF ||
                (human_line[0] && human_line[strlen(human_line) - 1u] != '\n' &&
                 fputc('\n', context->out) == EOF))
                return -1;
            return 0;
        }
        context->records_buffered = 1;
    }
    if (context->invocation->format == MAELYS_CLI_FORMAT_JSONL &&
        !context->invocation->field) {
        /* One line per record, held until maelys_cli_finish_records(): a
         * failure leaves stdout empty in every format (agent-cli/v2,
         * section 7), so no line leaves before the command knows it
         * succeeded. The buffer is the records writer, which jsonl uses for
         * nothing else; a trusted record is kept verbatim. */
        char *compact = NULL;
        if (!trusted && maelys_cli_json_format(record_json, 1, &compact) != 0) {
            context->records_failed = 1;
            return -1;
        }
        int failed = append_trusted(&context->records, trusted ? record_json : compact) != 0 ||
            append_trusted(&context->records, "\n") != 0;
        free(compact);
        if (failed) context->records_failed = 1;
        return failed ? -1 : 0;
    }
    /* TEXT, JSON, and JSONL held back for --field: buffer into an array,
     * exactly as JSON always has, so maelys_cli_finish_records() can build
     * the {"count","records"} object the field renderer selects from. */
    switch (context->invocation->format) {
        case MAELYS_CLI_FORMAT_JSONL:
        case MAELYS_CLI_FORMAT_TEXT:
        case MAELYS_CLI_FORMAT_JSON:
            if (context->record_count == 1u &&
                maelys_cli_json_begin_array(&context->records) != 0)
                return -1;
            if (trusted) {
                if (context->record_count > 1u &&
                    append_trusted(&context->records, ",") != 0)
                    return -1;
                context->records.has_items[context->records.depth - 1u] = 1u;
                if (append_trusted(&context->records, record_json) != 0) {
                    context->records_failed = 1;
                    return -1;
                }
                return 0;
            }
            if (maelys_cli_json_raw(&context->records, record_json) != 0) {
                context->records_failed = 1;
                return -1;
            }
            return 0;
    }
    return -1;
}

/* ---- --expect, a plan bound to its application (spec 2.9, section 4) ----- */

int maelys_cli_expect(maelys_cli_context_t *context, const char *fingerprint) {
    if (!context || !context->invocation || !fingerprint)
        return MAELYS_CLI_EXIT_FAILURE;
    context->expect_checked = 1;
    const char *expected = maelys_cli_option(context, "expect");
    if (!expected || !strcmp(expected, fingerprint)) return 0;
    return maelys_cli_fail(context, MAELYS_CLI_CODE_PRECONDITION_FAILED,
        "Plan again without --apply, review the new plan, then apply it with "
        "its fingerprint.",
        "The plan --expect names is no longer the one '%s' would apply: the "
        "action or the state it touches has changed. Nothing was written.",
        command_id(context));
}

/* ---- text records, pipe form (spec 2.3, section 7) ---------------------- */

typedef struct text_columns {
    char **names;
    size_t count;
    size_t capacity;
} text_columns_t;

static int column_add(text_columns_t *columns, const char *name) {
    for (size_t i = 0u; i < columns->count; ++i)
        if (!strcmp(columns->names[i], name)) return 0;
    if (columns->count == columns->capacity) {
        size_t capacity = columns->capacity ? columns->capacity * 2u : 8u;
        char **grown = realloc(columns->names, capacity * sizeof(*grown));
        if (!grown) return -1;
        columns->names = grown;
        columns->capacity = capacity;
    }
    columns->names[columns->count] = strdup(name);
    if (!columns->names[columns->count]) return -1;
    columns->count++;
    return 0;
}

static int compare_names(const void *left, const void *right) {
    /* strcmp on UTF-8 orders by Unicode code point. */
    return strcmp(*(char *const *)left, *(char *const *)right);
}

/* Visits the members of the object at text[offset]; callback receives the
 * decoded key and the raw value span. Returns -1 on malformed input. */
static int visit_members(
    const char *text, size_t length, size_t offset,
    int (*callback)(void *state, const char *key, const char *value, size_t value_length),
    void *state) {
    size_t cursor = offset;
    if (cursor >= length || text[cursor] != '{') return -1;
    ++cursor;
    for (;;) {
        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\n' ||
               text[cursor] == '\r' || text[cursor] == '\t'))
            ++cursor;
        if (cursor >= length) return -1;
        if (text[cursor] == '}') return 0;
        if (text[cursor] == ',') { ++cursor; continue; }
        if (text[cursor] != '"') return -1;
        size_t key_end = maelys_cli_json_value_end(text, length, cursor);
        if (key_end == 0u) return -1;
        char *key = maelys_cli_json_string_decode(text, length, cursor);
        if (!key) return -1;
        cursor = key_end;
        while (cursor < length && text[cursor] != ':') ++cursor;
        if (cursor >= length) { free(key); return -1; }
        ++cursor;
        while (cursor < length && (text[cursor] == ' ' || text[cursor] == '\n' ||
               text[cursor] == '\r' || text[cursor] == '\t'))
            ++cursor;
        size_t value_end = maelys_cli_json_value_end(text, length, cursor);
        if (value_end == 0u) { free(key); return -1; }
        int result = callback(state, key, text + cursor, value_end - cursor);
        free(key);
        if (result != 0) return -1;
        cursor = value_end;
    }
}

static int collect_column(void *state, const char *key, const char *value, size_t value_length) {
    (void)value;
    (void)value_length;
    return column_add((text_columns_t *)state, key);
}

typedef struct row_state {
    const text_columns_t *columns;
    const char **values;
    size_t *lengths;
} row_state_t;

static int collect_cell(void *state, const char *key, const char *value, size_t value_length) {
    row_state_t *row = (row_state_t *)state;
    for (size_t i = 0u; i < row->columns->count; ++i) {
        if (!strcmp(row->columns->names[i], key)) {
            row->values[i] = value;
            row->lengths[i] = value_length;
            return 0;
        }
    }
    return 0;
}

static int write_cell(FILE *stream, const char *value, size_t value_length) {
    if (!value) return 0;
    if (value[0] == '"') {
        char *decoded = maelys_cli_json_string_decode(value, value_length, 0u);
        if (!decoded) return -1;
        int failed = 0;
        for (const unsigned char *p = (const unsigned char *)decoded; *p && !failed; ++p) {
            if (*p == '\\') failed = fputs("\\\\", stream) == EOF;
            else if (*p == '\t') failed = fputs("\\t", stream) == EOF;
            else if (*p == '\r') failed = fputs("\\r", stream) == EOF;
            else if (*p == '\n') failed = fputs("\\n", stream) == EOF;
            else if (*p < 0x20u || *p == 0x7fu)
                failed = fprintf(stream, "\\u%04x", (unsigned int)*p) < 0;
            else failed = fputc(*p, stream) == EOF;
        }
        free(decoded);
        return failed ? -1 : 0;
    }
    char *copy = malloc(value_length + 1u);
    if (!copy) return -1;
    memcpy(copy, value, value_length);
    copy[value_length] = '\0';
    char *compact = NULL;
    int result = maelys_cli_json_format(copy, 1, &compact);
    free(copy);
    if (result != 0) return -1;
    int failed = fputs(compact, stream) == EOF;
    free(compact);
    return failed ? -1 : 0;
}

/* Renders the buffered records as tab-separated rows: the columns are the
 * union of the member names sorted by code point, a missing member is an
 * empty field, strings are unquoted and escaped, other values compact JSON. */
static int write_text_records(maelys_cli_context_t *context, const char *array) {
    size_t length = strlen(array);
    text_columns_t columns = {NULL, 0u, 0u};
    int result = 0;
    size_t cursor = 1u; /* after '[' */
    /* First pass: the columns. */
    for (;;) {
        while (cursor < length && (array[cursor] == ' ' || array[cursor] == ',' ||
               array[cursor] == '\n')) ++cursor;
        if (cursor >= length || array[cursor] == ']') break;
        if (array[cursor] != '{') { result = -1; break; }
        size_t end = maelys_cli_json_value_end(array, length, cursor);
        if (end == 0u || visit_members(array, length, cursor, collect_column, &columns) != 0) {
            result = -1;
            break;
        }
        cursor = end;
    }
    if (result == 0 && columns.count > 1u)
        qsort(columns.names, columns.count, sizeof(*columns.names), compare_names);
    /* Second pass: the rows. */
    cursor = 1u;
    const char **values = result == 0 && columns.count ? calloc(columns.count, sizeof(*values)) : NULL;
    size_t *lengths = result == 0 && columns.count ? calloc(columns.count, sizeof(*lengths)) : NULL;
    if (result == 0 && columns.count && (!values || !lengths)) result = -1;
    while (result == 0) {
        while (cursor < length && (array[cursor] == ' ' || array[cursor] == ',' ||
               array[cursor] == '\n')) ++cursor;
        if (cursor >= length || array[cursor] == ']') break;
        size_t end = maelys_cli_json_value_end(array, length, cursor);
        for (size_t i = 0u; values && lengths && i < columns.count; ++i) { values[i] = NULL; lengths[i] = 0u; }
        row_state_t row = {&columns, values, lengths};
        if (end == 0u || visit_members(array, length, cursor, collect_cell, &row) != 0) {
            result = -1;
            break;
        }
        for (size_t i = 0u; values && lengths && i < columns.count && result == 0; ++i) {
            if (i && fputc('\t', context->out) == EOF) result = -1;
            if (result == 0 && write_cell(context->out, values[i], lengths[i]) != 0) result = -1;
        }
        if (result == 0 && fputc('\n', context->out) == EOF) result = -1;
        cursor = end;
    }
    free(values);
    free(lengths);
    for (size_t i = 0u; i < columns.count; ++i) free(columns.names[i]);
    free(columns.names);
    return result;
}

/* ---- --field, one member of data (spec 2.4) ------------------------------ */

/* Calls callback for each top-level element of the JSON array whose source
 * starts at array[0] == '['. Returns -1 on malformed input, or when the
 * callback itself does. */
static int iterate_array(
    const char *array, size_t length,
    int (*callback)(void *state, const char *element, size_t element_length),
    void *state) {
    if (length == 0u || array[0] != '[') return -1;
    size_t cursor = 1u;
    for (;;) {
        while (cursor < length && (array[cursor] == ' ' || array[cursor] == ',' ||
               array[cursor] == '\n' || array[cursor] == '\r' || array[cursor] == '\t'))
            ++cursor;
        if (cursor >= length) return -1;
        if (array[cursor] == ']') return 0;
        size_t end = maelys_cli_json_value_end(array, length, cursor);
        if (end == 0u) return -1;
        if (callback(state, array + cursor, end - cursor) != 0) return -1;
        cursor = end;
    }
}

typedef struct all_objects_state {
    int all;
    int any;
} all_objects_state_t;

static int mark_if_not_object(void *state_ptr, const char *element, size_t element_length) {
    all_objects_state_t *state = state_ptr;
    state->any = 1;
    if (element_length == 0u || element[0] != '{') state->all = 0;
    return 0;
}

/* True for an array with at least one element, all of them objects (spec:
 * "an array whose every element is an object"). An empty array is false,
 * as either branch renders it as no lines. */
static int array_is_all_objects(const char *array, size_t length) {
    all_objects_state_t state = {1, 0};
    if (iterate_array(array, length, mark_if_not_object, &state) != 0) return 0;
    return state.any && state.all;
}

/* Wraps a JSON value span with '[' ']' into a fresh NUL-terminated buffer,
 * so write_text_records() can render an object as the one-row array of
 * itself the field rules ask for. */
static char *bracket(const char *value, size_t length) {
    char *wrapped = malloc(length + 3u);
    if (!wrapped) return NULL;
    wrapped[0] = '[';
    memcpy(wrapped + 1, value, length);
    wrapped[length + 1u] = ']';
    wrapped[length + 2u] = '\0';
    return wrapped;
}

typedef struct write_line_state {
    FILE *out;
    int failed;
} write_line_state_t;

static int write_array_line_text(void *state_ptr, const char *element, size_t element_length) {
    write_line_state_t *state = state_ptr;
    if (write_cell(state->out, element, element_length) != 0 ||
        fputc('\n', state->out) == EOF)
        state->failed = 1;
    return state->failed ? -1 : 0;
}

static int write_array_line_jsonl(void *state_ptr, const char *element, size_t element_length) {
    write_line_state_t *state = state_ptr;
    char *copy = malloc(element_length + 1u);
    if (!copy) { state->failed = 1; return -1; }
    memcpy(copy, element, element_length);
    copy[element_length] = '\0';
    char *compact = NULL;
    int formatted = maelys_cli_json_format(copy, 1, &compact);
    free(copy);
    if (formatted != 0) { state->failed = 1; return -1; }
    if (fputs(compact, state->out) == EOF || fputc('\n', state->out) == EOF)
        state->failed = 1;
    free(compact);
    return state->failed ? -1 : 0;
}

/* Renders one field value in text mode: the section 7 pipe rules, extended
 * by section 5 to every shape. An array whose every element is an object
 * gives one row per object (write_text_records, unmodified: --field records
 * on a json-records command equals its existing rendering); any other
 * array gives one value per line; an object gives one row, its members as
 * columns; anything else gives its escaped value on one line. */
static int render_field_text(
    maelys_cli_context_t *context, const char *value, size_t length) {
    if (length && value[0] == '[') {
        if (array_is_all_objects(value, length)) {
            char *copy = malloc(length + 1u);
            if (!copy) return -1;
            memcpy(copy, value, length);
            copy[length] = '\0';
            int result = write_text_records(context, copy);
            free(copy);
            return result;
        }
        write_line_state_t state = {context->out, 0};
        return iterate_array(value, length, write_array_line_text, &state) != 0 ||
            state.failed ? -1 : 0;
    }
    if (length && value[0] == '{') {
        char *wrapped = bracket(value, length);
        if (!wrapped) return -1;
        int result = write_text_records(context, wrapped);
        free(wrapped);
        return result;
    }
    return write_cell(context->out, value, length) != 0 ||
        fputc('\n', context->out) == EOF ? -1 : 0;
}

/* Renders one field value in jsonl mode: an array gives one compact JSON
 * value per line, anything else exactly one line, the rendering total so
 * validity never depends on the data (spec 2.4). */
static int render_field_jsonl(
    maelys_cli_context_t *context, const char *value, size_t length) {
    if (length && value[0] == '[') {
        write_line_state_t state = {context->out, 0};
        return iterate_array(value, length, write_array_line_jsonl, &state) != 0 ||
            state.failed ? -1 : 0;
    }
    char *copy = malloc(length + 1u);
    if (!copy) return -1;
    memcpy(copy, value, length);
    copy[length] = '\0';
    char *compact = NULL;
    int formatted = maelys_cli_json_format(copy, 1, &compact);
    free(copy);
    if (formatted != 0) return -1;
    int failed = fputs(compact, context->out) == EOF || fputc('\n', context->out) == EOF;
    free(compact);
    return failed ? -1 : 0;
}

typedef struct find_member_state {
    const char *name;
    const char *value;
    size_t length;
    int found;
} find_member_state_t;

static int match_member(
    void *state_ptr, const char *key, const char *value, size_t value_length) {
    find_member_state_t *state = state_ptr;
    if (!state->found && !strcmp(key, state->name)) {
        state->value = value;
        state->length = value_length;
        state->found = 1;
    }
    return 0;
}

typedef struct required_state {
    const char *quoted;
    size_t length;
    int found;
} required_state_t;

static int match_required(
    void *state_ptr, const char *element, size_t element_length) {
    required_state_t *state = state_ptr;
    if (element_length == state->length &&
        !memcmp(element, state->quoted, element_length))
        state->found = 1;
    return 0;
}

/* The top-level `required` array of the command's output schema, as its
 * source span; 0 when the schema has none. */
static int schema_required(
    const maelys_cli_command_t *command, const char **out_array, size_t *out_length) {
    const char *schema = command->output_schema_json;
    if (!schema) return 0;
    while (*schema == ' ' || *schema == '\t' || *schema == '\n' || *schema == '\r')
        ++schema;
    find_member_state_t required = {"required", NULL, 0u, 0};
    if (visit_members(schema, strlen(schema), 0u, match_member, &required) != 0 ||
        !required.found || required.length == 0u || required.value[0] != '[')
        return 0;
    *out_array = required.value;
    *out_length = required.length;
    return 1;
}

/* 1 when the top-level `required` of the command's output schema lists
 * `name`. The name is compared in its JSON spelling, so a schema that
 * escapes a letter it need not escape does not match. */
static int schema_requires(const maelys_cli_command_t *command, const char *name) {
    const char *array = NULL;
    size_t length = 0u;
    if (!schema_required(command, &array, &length)) return 0;
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    char *quoted = maelys_cli_json_string(&writer, name) == 0 ?
        maelys_cli_json_finish(&writer) : NULL;
    if (!quoted) {
        maelys_cli_json_writer_clear(&writer);
        return 0;
    }
    required_state_t state = {quoted, strlen(quoted), 0};
    (void)iterate_array(array, length, match_required, &state);
    free(quoted);
    return state.found;
}

/* The refusals --field brings that the parser could not make, decided on the
 * catalog and before the handler (agent-cli/v2 2.9, section 5): returns 1
 * with the error. A caller that reads VALIDATION_FAILED concludes that
 * nothing changed, so none of these may follow a write.
 *  - the format MAELYS_CLI_FORMAT selected is json: the conflict the parser
 *    refuses when --format json is explicit;
 *  - the command may write -- a transaction, with or without --apply, or an
 *    execute -- and the name is not in the top-level `required` of its
 *    output schema. A member the schema leaves optional is refused even
 *    when this run would have carried it, and a schema that requires none
 *    accepts no --field. A read decides on data, in reply_field(). */
static int field_refused(
    const maelys_cli_invocation_t *invocation, maelys_cli_error_t *error) {
    const maelys_cli_command_t *command = invocation->command;
    if (!invocation->field || command->output == MAELYS_CLI_OUTPUT_STREAM)
        return 0;
    if (invocation->format == MAELYS_CLI_FORMAT_JSON) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use --format text or --format jsonl with --field.",
            "--field conflicts with --format json: a filtered envelope "
            "would not validate against the command's outputSchema.");
        return 1;
    }
    int may_write = command->apply_effect != MAELYS_CLI_EFFECT_NONE ||
        command->effect == MAELYS_CLI_EFFECT_APPLY ||
        command->effect == MAELYS_CLI_EFFECT_COMMIT ||
        command->effect == MAELYS_CLI_EFFECT_EXECUTE;
    if (!may_write) return 0;
    const char *array = NULL;
    size_t length = 0u;
    if (!schema_required(command, &array, &length)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Run the command without --field and read the member from its result.",
            "Option --field is not accepted by '%s': the command can write and "
            "its output schema requires no member.", command->id);
        return 1;
    }
    if (schema_requires(command, invocation->field)) return 0;
    maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
        "Use a member the command's output schema requires; describe lists them.",
        "Option --field names '%s', which '%s' does not always return: a "
        "command that can write accepts only a member its output schema "
        "requires.", invocation->field, command->id);
    return 1;
}

/* Applies --field NAME to a complete, valid JSON object `data`: renders the
 * named top-level member by the active format (text or jsonl; json was
 * refused earlier by the parser whenever the option was explicit) and
 * returns exit_code, or replies VALIDATION_FAILED when the name is absent
 * -- discoverable only now, once the handler has produced data, unlike
 * every other rendering refusal. context->replied is already 1 on entry
 * (succeed_with/finish_records set it before calling this); the failure
 * path resets it, as the invalid-JSON defect path already does. */
static int reply_field(
    maelys_cli_context_t *context, const char *data, int exit_code) {
    const char *name = context->invocation->field;
    if (context->invocation->format == MAELYS_CLI_FORMAT_JSON) {
        /* Defensive: maelys_cli_run() refuses this before the handler, and
         * the parser an explicit --format json. Reached only by a caller
         * that builds its own context. */
        maelys_cli_error_t error;
        context->replied = 0;
        maelys_cli_error_set(&error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use --format text or --format jsonl with --field.",
            "--field conflicts with --format json: a filtered envelope "
            "would not validate against the command's outputSchema.");
        return maelys_cli_fail_error(context, &error);
    }
    find_member_state_t state = {name, NULL, 0u, 0};
    if (visit_members(data, strlen(data), 0u, match_member, &state) != 0 ||
        !state.found) {
        maelys_cli_error_t error;
        context->replied = 0;
        maelys_cli_error_set(&error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use a top-level member of the command's data.",
            "Option --field names '%s', which '%s' does not have.",
            name, command_id(context));
        return maelys_cli_fail_error(context, &error);
    }
    int written = context->invocation->format == MAELYS_CLI_FORMAT_JSONL ?
        render_field_jsonl(context, state.value, state.length) :
        render_field_text(context, state.value, state.length);
    if (written != 0 || fflush(context->out) != 0) return MAELYS_CLI_EXIT_FAILURE;
    return exit_code;
}

int maelys_cli_emit_record(
    maelys_cli_context_t *context, const char *record_json,
    const char *human_line) {
    return emit_record_with(context, record_json, human_line, 0);
}

int maelys_cli_emit_record_trusted(
    maelys_cli_context_t *context, const char *record_json,
    const char *human_line) {
    return emit_record_with(context, record_json, human_line, 1);
}

int maelys_cli_finish_records(maelys_cli_context_t *context, int exit_code) {
    if (!context || !context->invocation) return MAELYS_CLI_EXIT_FAILURE;
    if (context->records_failed) {
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED,
            "Report this defect to the command implementation.",
            "Command '%s' emitted an invalid record.", command_id(context));
    }
    if (!context->invocation->field &&
        context->invocation->format == MAELYS_CLI_FORMAT_TEXT &&
        context->records_buffered && context->record_count > 0u) {
        if (maelys_cli_json_end_array(&context->records) != 0) {
            maelys_cli_json_writer_clear(&context->records);
            return MAELYS_CLI_EXIT_FAILURE;
        }
        char *array = maelys_cli_json_finish(&context->records);
        if (!array) return MAELYS_CLI_EXIT_FAILURE;
        int written = write_text_records(context, array);
        free(array);
        if (written != 0)
            return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED,
                "Report this defect to the command implementation.",
                "Command '%s' emitted a record that is not an object.",
                command_id(context));
        return maelys_cli_succeed(context, "{}", "", exit_code);
    }
    if (!context->invocation->field &&
        context->invocation->format == MAELYS_CLI_FORMAT_JSONL &&
        context->record_count > 0u) {
        /* The lines emit_record_with() held, now that the command succeeds. */
        char *lines = maelys_cli_json_finish(&context->records);
        if (!lines) return MAELYS_CLI_EXIT_FAILURE;
        int failed = fputs(lines, context->out) == EOF;
        free(lines);
        if (failed) return MAELYS_CLI_EXIT_FAILURE;
        return maelys_cli_succeed(context, "{}", "", exit_code);
    }
    /* --field needs count and records as named members of one object even
     * in text or jsonl, where this function otherwise never builds one:
     * only the field renderer, in succeed_with(), knows which to keep. */
    if (!context->invocation->field &&
        context->invocation->format != MAELYS_CLI_FORMAT_JSON)
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
    maelys_cli_progress_done(context);
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
    (void)fprintf(err, "%s%s: [%s]%s ",
        maelys_cli_style(color, MAELYS_CLI_STYLE_ERROR), program, error->code,
        maelys_cli_style(color, MAELYS_CLI_STYLE_RESET));
    maelys_cli_fprint_terminal_safe(err, error->message);
    (void)fputc('\n', err);
    if (error->hint[0]) {
        (void)fputs("Hint: ", err);
        maelys_cli_fprint_terminal_safe(err, error->hint);
        (void)fputc('\n', err);
    }
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

int maelys_cli_fail_file(
    maelys_cli_context_t *context, int saved_errno, const char *explanation,
    const char *what) {
    maelys_cli_error_t error;
    maelys_cli_error_from_errno(&error, maelys_cli_file_error_code(saved_errno),
        saved_errno, what);
    if (explanation) {
        (void)snprintf(error.hint, sizeof(error.hint), "%s.", explanation);
        if (error.hint[0]) error.hint[0] = (char)toupper((unsigned char)error.hint[0]);
    }
    return maelys_cli_fail_error(context, &error);
}

void maelys_cli_warn(maelys_cli_context_t *context, const char *format, ...) {
    maelys_cli_progress_done(context);
    FILE *err = context && context->err ? context->err : stderr;
    const char *program = context && context->app ? context->app->program : "maelys";
    int color = context ? context->terminal.color_stderr : 0;
    (void)fprintf(err, "%s%s: warning:%s ",
        maelys_cli_style(color, MAELYS_CLI_STYLE_WARNING), program,
        maelys_cli_style(color, MAELYS_CLI_STYLE_RESET));
    if (format) {
        char message[MAELYS_CLI_MAX_ERROR_MESSAGE];
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);
        maelys_cli_fprint_terminal_safe(err, message);
    }
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
    maelys_cli_fprint_terminal_safe(context->err, question);
    (void)fputs(" [y/N] ", context->err);
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

/* An operand name is UPPER_CASE by convention; option names are lower-case.
 * conflicts_with names an operand when its spelling is not an option name. */
static int conflicts_with_operand(const maelys_cli_option_t *option) {
    if (!option->conflicts_with) return 0;
    for (const char *p = option->conflicts_with; *p; ++p)
        if (*p >= 'A' && *p <= 'Z') return 1;
    return 0;
}

/* Members shared by option arguments and typed operands. */
static int describe_value_type(
    maelys_cli_json_writer_t *writer, const maelys_cli_option_t *option) {
    {
        if (maelys_cli_json_key_string(writer, "type",
                maelys_cli_value_kind_name(option->kind)) != 0)
            return -1;
        if (option->kind == MAELYS_CLI_VALUE_DIGEST && option->choices) {
            if (maelys_cli_json_key(writer, "algorithms") != 0 ||
                maelys_cli_json_begin_array(writer) != 0)
                return -1;
            for (size_t i = 0u; option->choices[i]; ++i)
                if (maelys_cli_json_string(writer, option->choices[i]) != 0)
                    return -1;
            if (maelys_cli_json_end_array(writer) != 0) return -1;
        }
        if (option->kind == MAELYS_CLI_VALUE_CHOICE && option->choices) {
            if (maelys_cli_json_key(writer, "choices") != 0 ||
                maelys_cli_json_begin_array(writer) != 0)
                return -1;
            for (size_t i = 0u; option->choices[i]; ++i)
                if (maelys_cli_json_string(writer, option->choices[i]) != 0)
                    return -1;
            if (maelys_cli_json_end_array(writer) != 0) return -1;
        }
        /* An unsigned kind states a bound only when the declaration does: a
         * minimum of 0 is the kind's own floor, and a maximum of 0 means
         * unbounded, which an absent member says and UINT64_MAX — what this
         * wrote until 0.5.29 — does not, in a number above 2^53 that a
         * JavaScript reader cannot hold exactly. */
        if (option->kind == MAELYS_CLI_VALUE_UNSIGNED ||
            option->kind == MAELYS_CLI_VALUE_SIZE ||
            option->kind == MAELYS_CLI_VALUE_DURATION) {
            if (option->minimum &&
                maelys_cli_json_key_unsigned(writer, "minimum", option->minimum) != 0)
                return -1;
            if (option->maximum &&
                maelys_cli_json_key_unsigned(writer, "maximum", option->maximum) != 0)
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
        if (option->pattern &&
            maelys_cli_json_key_string(writer, "pattern", option->pattern) != 0)
            return -1;
        /* One accepted length is the number; two are the array the contract
         * already declares (`digits` is integer or array of integers). The
         * second length used to be an `alternativeDigits` member of its own,
         * which no schema allowed: MAELYS_CLI_HEX_OR is declared by no
         * product of this repository, so the conformance kit, which judges
         * products, never saw it. */
        if (option->kind == MAELYS_CLI_VALUE_HEX) {
            if (option->hex_digits_alternative) {
                if (maelys_cli_json_key(writer, "digits") != 0 ||
                    maelys_cli_json_begin_array(writer) != 0 ||
                    maelys_cli_json_unsigned(writer,
                        (uint64_t)option->hex_digits) != 0 ||
                    maelys_cli_json_unsigned(writer,
                        (uint64_t)option->hex_digits_alternative) != 0 ||
                    maelys_cli_json_end_array(writer) != 0)
                    return -1;
            } else if (maelys_cli_json_key_unsigned(writer, "digits",
                    (uint64_t)option->hex_digits) != 0) {
                return -1;
            }
        }
    }
    return 0;
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
            describe_value_type(writer, option) != 0 ||
            maelys_cli_json_end_object(writer) != 0)
            return -1;
    }
    if (option->default_text &&
        maelys_cli_json_key_string(writer, "default", option->default_text) != 0)
        return -1;
    if (maelys_cli_json_key(writer, "requires") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    if (option->depends_on) {
        char required[MAELYS_CLI_MAX_OPTION_NAME + 2u];
        (void)snprintf(required, sizeof(required), "--%s", option->depends_on);
        if (maelys_cli_json_string(writer, required) != 0) return -1;
    }
    for (size_t d = 0u; option->depends_on_all && option->depends_on_all[d]; ++d) {
        char required[MAELYS_CLI_MAX_OPTION_NAME + 2u];
        (void)snprintf(required, sizeof(required), "--%s", option->depends_on_all[d]);
        if (maelys_cli_json_string(writer, required) != 0) return -1;
    }
    if (maelys_cli_json_end_array(writer) != 0 ||
        maelys_cli_json_key(writer, "conflictsWith") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    if (option->conflicts_with) {
        char conflict[MAELYS_CLI_MAX_OPTION_NAME + 2u];
        (void)snprintf(conflict, sizeof(conflict), "%s%s",
            conflicts_with_operand(option) ? "" : "--", option->conflicts_with);
        if (maelys_cli_json_string(writer, conflict) != 0) return -1;
    }
    if (maelys_cli_json_end_array(writer) != 0) return -1;
    if (option->group &&
        maelys_cli_json_key_string(writer, "group", option->group) != 0)
        return -1;
    /* Emitted only when true, so generated references do not change. */
    if (option->hidden &&
        maelys_cli_json_key_boolean(writer, "hidden", 1) != 0)
        return -1;
    return maelys_cli_json_end_object(writer) == 0 ? 0 : -1;
}

static int describe_constraints(
    maelys_cli_json_writer_t *writer, const maelys_cli_command_t *command) {
    if (maelys_cli_json_begin_array(writer) != 0) return -1;
    for (size_t i = 0u; i < command->option_count; ++i) {
        const maelys_cli_option_t *option = &command->options[i];
        for (size_t d = 0u; option->depends_on_all && option->depends_on_all[d]; ++d) {
            char first[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            char second[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            (void)snprintf(first, sizeof(first), "--%s", option->name);
            (void)snprintf(second, sizeof(second), "--%s", option->depends_on_all[d]);
            if (maelys_cli_json_begin_object(writer) != 0 ||
                maelys_cli_json_key_string(writer, "kind", "requires") != 0 ||
                maelys_cli_json_key(writer, "options") != 0 ||
                maelys_cli_json_begin_array(writer) != 0 ||
                maelys_cli_json_string(writer, first) != 0 ||
                maelys_cli_json_string(writer, second) != 0 ||
                maelys_cli_json_end_array(writer) != 0 ||
                maelys_cli_json_end_object(writer) != 0)
                return -1;
        }
        /* One all-or-none constraint per group, emitted at its first member. */
        if (option->group) {
            int first_member = 1;
            for (size_t j = 0u; j < i; ++j)
                if (command->options[j].group &&
                    !strcmp(command->options[j].group, option->group))
                    first_member = 0;
            /* No name on the entry: its options are the whole rule (spec
             * 2.5), and the name is already carried by each option's
             * `group`. The kit checks that entries and groups agree. */
            if (first_member) {
                if (maelys_cli_json_begin_object(writer) != 0 ||
                    maelys_cli_json_key_string(writer, "kind", "all-or-none") != 0 ||
                    maelys_cli_json_key(writer, "options") != 0 ||
                    maelys_cli_json_begin_array(writer) != 0)
                    return -1;
                for (size_t j = 0u; j < command->option_count; ++j) {
                    if (!command->options[j].group ||
                        strcmp(command->options[j].group, option->group))
                        continue;
                    char spelled[MAELYS_CLI_MAX_OPTION_NAME + 2u];
                    (void)snprintf(spelled, sizeof(spelled), "--%s",
                        command->options[j].name);
                    if (maelys_cli_json_string(writer, spelled) != 0) return -1;
                }
                if (maelys_cli_json_end_array(writer) != 0 ||
                    maelys_cli_json_end_object(writer) != 0)
                    return -1;
            }
        }
        const char *kinds[2] = {"requires", "at-most-one"};
        const char *targets[2] = {option->depends_on, option->conflicts_with};
        for (size_t k = 0u; k < 2u; ++k) {
            if (!targets[k]) continue;
            /* A conflict with an operand stays in conflictsWith only: the
             * entries of input.constraints name options. */
            if (k == 1u && conflicts_with_operand(option)) continue;
            char first[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            char second[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            (void)snprintf(first, sizeof(first), "--%s", option->name);
            (void)snprintf(second, sizeof(second), "%s%s",
                k == 1u && conflicts_with_operand(option) ? "" : "--", targets[k]);
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
    /* The rules the command states itself (spec 2.5), after the ones
     * derived from the option fields: exactly-one has no other site. */
    static const char *const declared_kinds[] = {
        "requires", "at-most-one", "exactly-one", "all-or-none"};
    for (size_t c = 0u; c < command->constraint_count; ++c) {
        const maelys_cli_constraint_t *rule = &command->constraints[c];
        if (maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "kind",
                declared_kinds[rule->kind]) != 0 ||
            maelys_cli_json_key(writer, "options") != 0 ||
            maelys_cli_json_begin_array(writer) != 0)
            return -1;
        for (size_t o = 0u; rule->options && rule->options[o]; ++o) {
            char spelled[MAELYS_CLI_MAX_OPTION_NAME + 2u];
            (void)snprintf(spelled, sizeof(spelled), "--%s", rule->options[o]);
            if (maelys_cli_json_string(writer, spelled) != 0) return -1;
        }
        if (maelys_cli_json_end_array(writer) != 0 ||
            maelys_cli_json_end_object(writer) != 0)
            return -1;
    }
    return maelys_cli_json_end_array(writer);
}

static int describe_command_body(
    maelys_cli_json_writer_t *writer, const maelys_cli_command_t *command,
    int summary, const char *synopsis);

static int describe_command(
    maelys_cli_json_writer_t *writer, const maelys_cli_command_t *command,
    int summary) {
    char *synopsis = maelys_cli_command_synopsis_alloc(command);
    if (!synopsis) return -1;
    int result = describe_command_body(writer, command, summary, synopsis);
    free(synopsis);
    return result;
}

static int describe_command_body(
    maelys_cli_json_writer_t *writer, const maelys_cli_command_t *command,
    int summary, const char *synopsis) {
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
        (command->protocol &&
         maelys_cli_json_key_string(writer, "protocol", command->protocol) != 0) ||
        maelys_cli_json_key_boolean(writer, "external", command->delegate != NULL) != 0 ||
        maelys_cli_json_key_boolean(writer, "hidden", command->hidden) != 0 ||
        maelys_cli_json_key_boolean(writer, "available", command->unavailable == NULL) != 0 ||
        (command->unavailable &&
         maelys_cli_json_key_string(writer, "unavailableReason",
            command->unavailable) != 0) ||
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
            maelys_cli_json_key_string(writer, "summary", operand->summary) != 0)
            return -1;
        if (operand->kind != MAELYS_CLI_VALUE_NONE) {
            maelys_cli_option_t spec;
            memset(&spec, 0, sizeof(spec));
            spec.kind = operand->kind;
            spec.choices = operand->choices;
            spec.minimum = operand->minimum;
            spec.maximum = operand->maximum;
            spec.signed_minimum = operand->signed_minimum;
            spec.signed_maximum = operand->signed_maximum;
            spec.hex_digits = operand->hex_digits;
            spec.hex_digits_alternative = operand->hex_digits_alternative;
            spec.pattern = operand->pattern;
            if (describe_value_type(writer, &spec) != 0) return -1;
        }
        if (maelys_cli_json_end_object(writer) != 0) return -1;
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

/* True when id is PREFIX itself or starts with "PREFIX." (namespace match,
 * never an arbitrary string prefix). */
static int in_namespace(const char *id, const char *prefix) {
    size_t length = strlen(prefix);
    return strncmp(id, prefix, length) == 0 &&
        (id[length] == '\0' || id[length] == '.');
}

static int describe_data(
    maelys_cli_context_t *context, const char *query, int summary,
    const char *prefix, maelys_cli_json_writer_t *writer) {
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
        maelys_cli_json_key_string(writer, "framework", MAELYS_CLI_VERSION) != 0)
        return -1;
    if (prefix) {
        if (maelys_cli_json_key(writer, "filter") != 0 ||
            maelys_cli_json_begin_object(writer) != 0 ||
            maelys_cli_json_key_string(writer, "kind", "command-prefix") != 0 ||
            maelys_cli_json_key_string(writer, "value", prefix) != 0 ||
            maelys_cli_json_end_object(writer) != 0)
            return -1;
    }
    if (maelys_cli_json_key(writer, "commands") != 0 ||
        maelys_cli_json_begin_array(writer) != 0)
        return -1;
    size_t count = maelys_cli_app_command_count(app);
    size_t matched = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        if (query && strcmp(query, command->id) != 0) continue;
        if (prefix && !in_namespace(command->id, prefix)) continue;
        if (describe_command(writer, command, summary) != 0) return -1;
        ++matched;
    }
    if (maelys_cli_json_end_array(writer) != 0) return -1;
    if ((query || prefix) && matched == 0u) return 1;
    /* A single descriptor and a summary stay minimal: the catalog-wide
     * members belong to the catalog form alone (spec 2.3, section 1). */
    if (query || prefix || summary)
        return maelys_cli_json_end_object(writer) == 0 ? 0 : -1;
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

/* Command-identifier grammar without a trailing dot:
 * ^[a-z](?:[a-z0-9.-]*[a-z0-9-])?$ */
static int valid_prefix(const char *prefix) {
    size_t length = strlen(prefix);
    if (length == 0u || prefix[0] < 'a' || prefix[0] > 'z') return 0;
    for (const char *p = prefix; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '-'))
            return 0;
    return prefix[length - 1u] != '.';
}

static int builtin_describe(maelys_cli_context_t *context) {
    const char *query = maelys_cli_operand(context, 0u);
    const char *prefix = maelys_cli_option(context, "prefix");
    int summary = maelys_cli_flag(context, "summary");
    if (prefix && !valid_prefix(prefix)) {
        return maelys_cli_fail(context, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use a command identifier prefix such as repo or repo.mirror.",
            "Invalid command prefix: %s.", prefix);
    }
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    int result = describe_data(context, query, summary, prefix, &writer);
    if (result == 1) {
        maelys_cli_json_writer_clear(&writer);
        if (prefix)
            return maelys_cli_fail(context, MAELYS_CLI_CODE_INVALID_COMMAND,
                "Run describe --summary --format json and select a returned "
                "command namespace.",
                "No command in namespace: %s.", prefix);
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

/* ---- completion ------------------------------------------------------------ */

static int locate_delegate(
    maelys_cli_context_t *context, const maelys_cli_command_t *command,
    char *out_path, size_t out_size, const char **out_explanation);

/* The three scripts are renderings of __complete (agent-cli/v2, section 6):
 * they offer its words and no others, and fall back to the shell's file
 * completion when it returns none. python/maelys_cli.py prints the same
 * text; a line that differs between the two is a defect.
 *
 * bash: the words are sliced before IFS becomes a newline. bash 3.2, which
 * macOS ships as /bin/bash, joins "${array[@]:offset:length}" into one word
 * when IFS holds no space. */
static const char *const bash_shim =
    "# bash completion for %1$s, generated from its catalog\n"
    "_%2$s_complete() {\n"
    "    local -a words\n"
    "    words=(\"${COMP_WORDS[@]:1:COMP_CWORD}\")\n"
    "    local IFS=$'\\n'\n"
    "    COMPREPLY=($(\"%1$s\" __complete -- \"${words[@]}\" 2>/dev/null))\n"
    "    if [ ${#COMPREPLY[@]} -eq 0 ]; then\n"
    "        COMPREPLY=($(compgen -f -- \"${COMP_WORDS[COMP_CWORD]}\"))\n"
    "    fi\n"
    "}\n"
    "complete -o filenames -F _%2$s_complete %1$s\n";

/* zsh: words[2,CURRENT] and not ${words[@]:1:CURRENT-1}, which zsh reads as
 * the modifier `C`; (f) on the quoted substitution, so that an empty answer
 * is an empty array and _files is reached. The last line serves both ways of
 * loading it: sourced after compinit it registers the function, autoloaded
 * from fpath under the name its #compdef line gives it completes at once. */
static const char *const zsh_shim =
    "#compdef %1$s\n"
    "# zsh completion for %1$s, generated from its catalog\n"
    "_%2$s_complete() {\n"
    "    local -a candidates\n"
    "    candidates=(${(f)\"$(\"%1$s\" __complete -- \"${(@)words[2,CURRENT]}\" 2>/dev/null)\"})\n"
    "    if (( ${#candidates} )); then\n"
    "        compadd -- \"${candidates[@]}\"\n"
    "    else\n"
    "        _files\n"
    "    fi\n"
    "}\n"
    "if [[ ${funcstack[1]} == _%1$s ]]; then\n"
    "    _%2$s_complete \"$@\"\n"
    "else\n"
    "    compdef _%2$s_complete %1$s\n"
    "fi\n";

/* fish: "$current" quoted, or an empty current word vanishes and the word
 * before it is completed instead; -f silences fish's own file completion,
 * so the function offers the paths itself when __complete returns none. */
static const char *const fish_shim =
    "# fish completion for %1$s, generated from its catalog\n"
    "function __%2$s_complete\n"
    "    set -l words (commandline -opc)\n"
    "    set -l current (commandline -ct)\n"
    "    set -l candidates (\"%1$s\" __complete -- $words[2..-1] \"$current\" 2>/dev/null)\n"
    "    if test (count $candidates) -gt 0\n"
    "        printf '%%s\\n' $candidates\n"
    "    else\n"
    "        __fish_complete_path \"$current\"\n"
    "    end\n"
    "end\n"
    "complete -c %1$s -f -a '(__%2$s_complete)'\n";

static void identifier_from_program(const char *program, char *out, size_t size) {
    size_t used = 0u;
    for (const char *p = program; *p && used + 1u < size; ++p) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9');
        out[used++] = ok ? c : '_';
    }
    out[used] = '\0';
}

static int builtin_completion(maelys_cli_context_t *context) {
    size_t shell = 0u;
    (void)maelys_cli_operand_choice(context, 0u, &shell);
    const char *shim = shell == 0u ? bash_shim : shell == 1u ? zsh_shim : fish_shim;
    char identifier[128];
    identifier_from_program(context->app->program, identifier, sizeof(identifier));
    char *script = NULL;
    size_t size = 0u;
    FILE *memory = open_memstream(&script, &size);
    if (!memory) return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_UNEXPECTED,
        errno, "completion buffer");
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    (void)fprintf(memory, shim, context->app->program, identifier);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    if (fclose(memory) != 0 || !script) {
        free(script);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not render the completion script.");
    }
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    int built = maelys_cli_json_begin_object(&data) == 0 &&
        maelys_cli_json_key_string(&data, "shell", shell_choices[shell]) == 0 &&
        maelys_cli_json_key_string(&data, "script", script) == 0 &&
        maelys_cli_json_end_object(&data) == 0;
    if (!built) {
        maelys_cli_json_writer_clear(&data);
        free(script);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not serialize the completion script.");
    }
    int result = maelys_cli_succeed_writer(context, &data, script, MAELYS_CLI_EXIT_OK);
    free(script);
    return result;
}

static int emit_candidate(maelys_cli_context_t *context, const char *word,
    const char *prefix) {
    if (strncmp(word, prefix, strlen(prefix)) != 0) return 0;
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    if (maelys_cli_json_begin_object(&writer) != 0 ||
        maelys_cli_json_key_string(&writer, "word", word) != 0 ||
        maelys_cli_json_end_object(&writer) != 0) {
        maelys_cli_json_writer_clear(&writer);
        return -1;
    }
    char *record = maelys_cli_json_finish(&writer);
    if (!record) return -1;
    int emitted = maelys_cli_emit_record(context, record, word);
    free(record);
    return emitted;
}

static int emit_option_candidates(maelys_cli_context_t *context,
    const maelys_cli_option_t *options, size_t count, const char *prefix,
    const maelys_cli_invocation_t *given) {
    for (size_t i = 0u; i < count; ++i) {
        if (options[i].hidden) continue;
        if (!options[i].repeatable &&
            maelys_cli_invocation_option(given, options[i].name))
            continue;
        char spelled[MAELYS_CLI_MAX_OPTION_NAME + 2u];
        (void)snprintf(spelled, sizeof(spelled), "--%s", options[i].name);
        if (emit_candidate(context, spelled, prefix) != 0) return -1;
    }
    return 0;
}

static int emit_value_candidates(maelys_cli_context_t *context,
    maelys_cli_value_kind_t kind, const char *const *choices,
    const char *prefix) {
    if (kind == MAELYS_CLI_VALUE_CHOICE && choices) {
        for (size_t i = 0u; choices[i]; ++i)
            if (emit_candidate(context, choices[i], prefix) != 0) return -1;
    } else if (kind == MAELYS_CLI_VALUE_DIGEST && choices) {
        for (size_t i = 0u; choices[i]; ++i) {
            char spelled[128];
            (void)snprintf(spelled, sizeof(spelled), "%s:", choices[i]);
            if (emit_candidate(context, spelled, prefix) != 0) return -1;
        }
    }
    /* Paths and free values fall back to the shell's file completion; so
     * does an operand declared without a kind, whose kind reads as the
     * flag's and is no boolean. */
    return 0;
}

/* The pattern word a command offers after the words given so far: 1 with
 * the word when the command is shown, available, starts with those words and
 * has one more; 0 otherwise. */
static int next_pattern_word(const maelys_cli_command_t *candidate,
    char *const given[], size_t given_count, char *out, size_t size) {
    if (candidate->hidden || candidate->unavailable) return 0;
    const char *cursor = candidate->pattern;
    for (size_t w = 0u; w < given_count; ++w) {
        const char *end = strchr(cursor, ' ');
        if (!end) return 0;
        size_t length = (size_t)(end - cursor);
        if (strlen(given[w]) != length || memcmp(given[w], cursor, length) != 0)
            return 0;
        cursor = end + 1;
    }
    const char *end = strchr(cursor, ' ');
    size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
    if (length >= size) return 0;
    memcpy(out, cursor, length);
    out[length] = '\0';
    return 1;
}

/* A word a delegate returns is shown by a shell: one with a control byte is
 * dropped rather than relayed. */
static int candidate_is_plain(const char *word) {
    for (const unsigned char *p = (const unsigned char *)word; *p; ++p)
        if (*p < 0x20u || *p == 0x7fu) return 0;
    return 1;
}

#define DELEGATE_WORDS_MAX (1u << 20)

/* Runs the delegate's __complete and returns what it wrote on its standard
 * output, NUL-terminated, or NULL. The process module hands descriptors 0,
 * 1 and 2 over as they are, so descriptor 1 is a pipe for the time of the
 * fork and the caller's own again before anything is read. */
static char *delegate_words(
    maelys_cli_context_t *context, const char *path, char *const arguments[]) {
    (void)fflush(context->out);
    (void)fflush(stdout);
    int channel[2];
    if (pipe(channel) != 0) return NULL;
    int saved = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    if (saved < 0 || dup2(channel[1], STDOUT_FILENO) < 0) {
        if (saved >= 0) (void)close(saved);
        (void)close(channel[0]);
        (void)close(channel[1]);
        return NULL;
    }
    (void)close(channel[1]);
    maelys_cli_process_t *process = NULL;
    int started = maelys_cli_process_start(path, arguments, NULL, NULL, &process);
    (void)dup2(saved, STDOUT_FILENO);
    (void)close(saved);
    char *buffer = NULL;
    size_t used = 0u;
    if (started == 0) buffer = malloc(DELEGATE_WORDS_MAX + 1u);
    while (buffer && used < DELEGATE_WORDS_MAX) {
        ssize_t amount = read(channel[0], buffer + used, DELEGATE_WORDS_MAX - used);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) break;
        used += (size_t)amount;
    }
    (void)close(channel[0]);
    if (process) {
        maelys_cli_process_status_t status;
        (void)maelys_cli_process_wait(process, &status);
        maelys_cli_process_release(process);
    }
    if (buffer) buffer[used] = '\0';
    return buffer;
}

static int builtin_complete(maelys_cli_context_t *context) {
    size_t count = maelys_cli_operand_count(context);
    const char *prefix = count ? maelys_cli_operand(context, count - 1u) : "";
    size_t given_count = count ? count - 1u : 0u;
    char *given[MAELYS_CLI_MAX_OPERANDS];
    for (size_t i = 0u; i < given_count; ++i)
        given[i] = (char *)maelys_cli_operand(context, i);
    const maelys_cli_app_t *app = context->app;

    /* Resolve the command from the complete words only. */
    const maelys_cli_command_t *command = NULL;
    size_t command_words = 0u;
    size_t total = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < total; ++i) {
        const maelys_cli_command_t *candidate = maelys_cli_app_command_at(app, i);
        size_t words = maelys_cli_pattern_words(candidate->pattern);
        if (candidate->hidden || candidate->unavailable ||
            words <= command_words || words > given_count)
            continue;
        int matches = 1;
        const char *cursor = candidate->pattern;
        for (size_t w = 0u; w < words && matches; ++w) {
            const char *end = strchr(cursor, ' ');
            size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
            if (strlen(given[w]) != length || memcmp(given[w], cursor, length) != 0)
                matches = 0;
            cursor = end ? end + 1 : cursor + length;
        }
        if (matches) {
            command = candidate;
            command_words = words;
        }
    }

    if (!command) {
        /* Complete the next pattern word of every command that starts with
         * the words given so far, once: `agents install` and `agents status`
         * offer `agents` a single time. */
        for (size_t i = 0u; i < total; ++i) {
            char word[MAELYS_CLI_MAX_OPTION_NAME];
            if (!next_pattern_word(maelys_cli_app_command_at(app, i), given,
                    given_count, word, sizeof(word)))
                continue;
            int offered = 0;
            for (size_t j = 0u; j < i && !offered; ++j) {
                char earlier[MAELYS_CLI_MAX_OPTION_NAME];
                offered = next_pattern_word(maelys_cli_app_command_at(app, j),
                    given, given_count, earlier, sizeof(earlier)) &&
                    !strcmp(earlier, word);
            }
            if (offered) continue;
            if (emit_candidate(context, word, prefix) != 0)
                return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                    "Could not write candidates.");
        }
        return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
    }

    if (command->delegate) {
        /* The delegate owns the catalog of everything after its pattern:
         * its words are returned as this program's records, the same in
         * every format, and none when it is not installed or has none
         * (agent-cli/v2, section 9). Never this program's own options. */
        char path[PATH_MAX];
        if (locate_delegate(context, command, path, sizeof(path), NULL) != 0)
            return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
        size_t forwarded = count - command_words;
        char **arguments = calloc(forwarded + 6u, sizeof(*arguments));
        if (!arguments)
            return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_UNEXPECTED,
                ENOMEM, "argument vector");
        arguments[0] = path;
        arguments[1] = (char *)"__complete";
        arguments[2] = (char *)"--format";
        arguments[3] = (char *)"text";
        arguments[4] = (char *)"--";
        for (size_t i = 0u; i < forwarded; ++i)
            arguments[5u + i] = (char *)maelys_cli_operand(context, command_words + i);
        char *words = delegate_words(context, path, arguments);
        free(arguments);
        int emitted = 0;
        for (char *line = words; emitted == 0 && line;) {
            char *end = strchr(line, '\n');
            if (end) *end = '\0';
            if (*line && candidate_is_plain(line))
                emitted = emit_candidate(context, line, "");
            line = end ? end + 1 : NULL;
        }
        free(words);
        if (emitted != 0)
            return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                "Could not write candidates.");
        return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
    }

    /* Parse the words after the pattern leniently to know what is expected. */
    maelys_cli_invocation_t partial;
    maelys_cli_error_t ignored;
    int argc = (int)given_count;
    (void)maelys_cli_parse(app, argc, given, &partial, &ignored);
    if (!partial.command) partial.command = command;

    /* Is the previous word an option expecting a value? */
    if (given_count > command_words) {
        const char *previous = given[given_count - 1u];
        if (strncmp(previous, "--", 2u) == 0 && !strchr(previous, '=')) {
            const char *name = previous + 2;
            const maelys_cli_option_t *option = NULL;
            for (size_t i = 0u; i < command->option_count; ++i)
                if (!strcmp(command->options[i].name, name)) option = &command->options[i];
            size_t transport_count = 0u;
            const maelys_cli_option_t *transport = maelys_cli_transport_options(&transport_count);
            for (size_t i = 0u; !option && i < transport_count; ++i)
                if (!strcmp(transport[i].name, name)) option = &transport[i];
            if (option && option->kind != MAELYS_CLI_VALUE_NONE) {
                if (emit_value_candidates(context, option->kind, option->choices, prefix) != 0)
                    return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                        "Could not write candidates.");
                return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
            }
        }
    }

    if (strncmp(prefix, "--", 2u) == 0) {
        const char *equals = strchr(prefix, '=');
        if (equals) {
            /* --option=VALUE: complete the value with the option spelled. */
            size_t name_length = (size_t)(equals - prefix) - 2u;
            for (size_t i = 0u; i < command->option_count; ++i) {
                const maelys_cli_option_t *option = &command->options[i];
                if (option->hidden || strlen(option->name) != name_length ||
                    memcmp(option->name, prefix + 2, name_length) != 0 ||
                    option->kind != MAELYS_CLI_VALUE_CHOICE || !option->choices)
                    continue;
                for (size_t c = 0u; option->choices[c]; ++c) {
                    char spelled[256];
                    (void)snprintf(spelled, sizeof(spelled), "--%s=%s",
                        option->name, option->choices[c]);
                    if (emit_candidate(context, spelled, prefix) != 0)
                        return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED,
                            NULL, "Could not write candidates.");
                }
            }
            return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
        }
        size_t transport_count = 0u;
        const maelys_cli_option_t *transport = maelys_cli_transport_options(&transport_count);
        if (emit_option_candidates(context, command->options, command->option_count,
                prefix, &partial) != 0 ||
            (command->output != MAELYS_CLI_OUTPUT_STREAM &&
             emit_option_candidates(context, transport, transport_count, prefix,
                &partial) != 0))
            return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                "Could not write candidates.");
        return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
    }

    /* help and describe take a command identifier. */
    if ((!strcmp(command->id, "help") || !strcmp(command->id, "describe")) &&
        given_count == command_words) {
        for (size_t i = 0u; i < total; ++i) {
            const maelys_cli_command_t *candidate = maelys_cli_app_command_at(app, i);
            if (candidate->hidden || candidate->unavailable) continue;
            if (emit_candidate(context, candidate->id, prefix) != 0)
                return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                    "Could not write candidates.");
        }
        return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
    }

    /* Positional: the next operand's choices when it is typed. */
    size_t position = partial.command == command ? partial.operand_count : 0u;
    if (command->operand_count) {
        size_t slot = position < command->operand_count ? position :
            command->operand_count - 1u;
        const maelys_cli_operand_t *operand = &command->operands[slot];
        if (position < command->operand_count || operand->variadic) {
            if (emit_value_candidates(context, operand->kind, operand->choices,
                    prefix) != 0)
                return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                    "Could not write candidates.");
        }
    }
    return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
}

/* ---- help ---------------------------------------------------------------- */

/* The help is read by a person, in a terminal: it fits the width, puts a
 * description beside a short label and below a long one, and breaks a line
 * between words. The general help names each command by its pattern and its
 * purpose, on one line; the usage of a command is in its own help, and the
 * usage of a family in the family's. */
#define HELP_WIDTH 80u
#define HELP_WIDTH_MINIMUM 60u
#define HELP_WIDTH_MAXIMUM 100u
#define HELP_LABEL_MAXIMUM 28u

/* Columns a code point takes in a terminal: none for a combining mark or a
 * zero-width character, two for the East Asian wide and fullwidth ranges
 * and for emoji, one otherwise. python/maelys_cli.py holds the same table. */
static size_t codepoint_width(uint32_t c) {
    if ((c >= 0x0300u && c <= 0x036Fu) || (c >= 0x1AB0u && c <= 0x1AFFu) ||
        (c >= 0x1DC0u && c <= 0x1DFFu) || (c >= 0x20D0u && c <= 0x20FFu) ||
        (c >= 0xFE00u && c <= 0xFE0Fu) || (c >= 0xFE20u && c <= 0xFE2Fu) ||
        (c >= 0x200Bu && c <= 0x200Fu) || c == 0x2060u || c == 0xFEFFu)
        return 0u;
    if ((c >= 0x1100u && c <= 0x115Fu) || (c >= 0x2E80u && c <= 0xA4CFu) ||
        (c >= 0xAC00u && c <= 0xD7A3u) || (c >= 0xF900u && c <= 0xFAFFu) ||
        (c >= 0xFE30u && c <= 0xFE4Fu) || (c >= 0xFF00u && c <= 0xFF60u) ||
        (c >= 0xFFE0u && c <= 0xFFE6u) || (c >= 0x1F300u && c <= 0x1F64Fu) ||
        (c >= 0x1F900u && c <= 0x1F9FFu) || (c >= 0x20000u && c <= 0x3FFFDu))
        return 2u;
    return 1u;
}

/* Display width of `length` bytes of UTF-8; an ill-formed byte counts one. */
static size_t display_width(const char *text, size_t length) {
    size_t width = 0u;
    for (size_t i = 0u; i < length;) {
        unsigned char lead = (unsigned char)text[i];
        size_t extra = lead >= 0xF0u ? 3u : lead >= 0xE0u ? 2u : lead >= 0xC0u ? 1u : 0u;
        uint32_t c = extra == 3u ? lead & 0x07u : extra == 2u ? lead & 0x0Fu :
            extra == 1u ? lead & 0x1Fu : lead;
        size_t used = 1u;
        while (used <= extra && i + used < length &&
               ((unsigned char)text[i + used] & 0xC0u) == 0x80u) {
            c = (c << 6) | ((unsigned char)text[i + used] & 0x3Fu);
            ++used;
        }
        width += used == extra + 1u ? codepoint_width(c) : 1u;
        i += used;
    }
    return width;
}

static void help_indent(FILE *stream, size_t count) {
    for (size_t i = 0u; i < count; ++i) (void)fputc(' ', stream);
}

/* Writes text wrapped between words so that no line passes `width`: the
 * first line goes on at `column`, the following ones start after `indent`
 * spaces. With `groups`, a space inside [...] is no place to break, which
 * keeps an option and its value together in a usage. A word longer than a
 * line stands alone on it. Returns the column reached. */
static size_t help_wrap(FILE *stream, const char *text, size_t column,
                        size_t indent, size_t width, int groups) {
    size_t at = column;
    int first = 1;
    for (const char *cursor = text; *cursor;) {
        while (*cursor == ' ') ++cursor;
        if (!*cursor) break;
        const char *end = cursor;
        int depth = 0;
        for (; *end && (*end != ' ' || (groups && depth > 0)); ++end) {
            if (*end == '[') ++depth;
            else if (*end == ']' && depth > 0) --depth;
        }
        size_t length = (size_t)(end - cursor);
        size_t word = display_width(cursor, length);
        if (!first && at + 1u + word > width && at > indent) {
            (void)fputc('\n', stream);
            help_indent(stream, indent);
            at = indent;
        } else if (!first) {
            (void)fputc(' ', stream);
            ++at;
        }
        (void)fwrite(cursor, 1u, length, stream);
        at += word;
        first = 0;
        cursor = end;
    }
    return at;
}

/* One entry of a list: the label, then the text beside it when the label
 * fits its column, below it when it does not. `groups` is for a label that
 * is a usage. */
static void help_entry(FILE *stream, const char *label, const char *text,
                       size_t label_width, size_t width, int groups) {
    size_t wide = display_width(label, strlen(label));
    (void)fputs("  ", stream);
    if (wide <= label_width) {
        (void)fputs(label, stream);
        help_indent(stream, label_width - wide + 2u);
        (void)help_wrap(stream, text, label_width + 4u, label_width + 4u, width, 0);
    } else {
        (void)help_wrap(stream, label, 2u, 8u, width, groups);
        (void)fputc('\n', stream);
        help_indent(stream, 6u);
        (void)help_wrap(stream, text, 6u, 6u, width, 0);
    }
    (void)fputc('\n', stream);
}

/* A paragraph under a heading, indented by two. */
static void help_paragraph(FILE *stream, const char *text, size_t width, int groups) {
    (void)fputs("  ", stream);
    (void)help_wrap(stream, text, 2u, groups ? 6u : 2u, width, groups);
    (void)fputc('\n', stream);
}

/* The width help is rendered at: the terminal's when stdout is one, within
 * bounds that keep it readable; 80 anywhere else, so that what goes into a
 * pipe, a file or data.text does not depend on a window. */
static size_t help_width(const maelys_cli_context_t *context) {
    if (!context->terminal.stdout_is_tty || context->terminal.columns == 0u ||
        (context->invocation && context->invocation->format != MAELYS_CLI_FORMAT_TEXT))
        return HELP_WIDTH;
    size_t columns = context->terminal.columns;
    return columns < HELP_WIDTH_MINIMUM ? HELP_WIDTH_MINIMUM :
        columns > HELP_WIDTH_MAXIMUM ? HELP_WIDTH_MAXIMUM : columns;
}

/* "--name VALUE", "--name a|b" or "--name": how an option is spelled. */
static void option_label(const maelys_cli_option_t *option, char *out, size_t size) {
    char value[160];
    value[0] = '\0';
    if (option->kind == MAELYS_CLI_VALUE_CHOICE && option->choices &&
        !option->value_name) {
        size_t used = 0u;
        for (size_t i = 0u; option->choices[i]; ++i) {
            int written = snprintf(value + used, sizeof(value) - used, "%s%s",
                i ? "|" : " ", option->choices[i]);
            if (written < 0 || (size_t)written >= sizeof(value) - used) break;
            used += (size_t)written;
        }
    } else if (option->kind != MAELYS_CLI_VALUE_NONE) {
        (void)snprintf(value, sizeof(value), " %s",
            option->value_name ? option->value_name : "VALUE");
    }
    (void)snprintf(out, size, "--%s%s%s", option->name, value,
        option->repeatable ? " (repeatable)" : "");
}

static void option_text(const maelys_cli_option_t *option, char *out, size_t size) {
    size_t used = (size_t)snprintf(out, size, "%s", option->summary);
    if (used >= size) return;
    if (option->default_text)
        used += (size_t)snprintf(out + used, size - used, " Default: %s.",
            option->default_text);
    if (used < size && option->required)
        used += (size_t)snprintf(out + used, size - used, " Required.");
    if (used < size && option->depends_on)
        used += (size_t)snprintf(out + used, size - used, " Requires --%s.",
            option->depends_on);
    if (used < size && option->conflicts_with)
        (void)snprintf(out + used, size - used, " Conflicts with --%s.",
            option->conflicts_with);
}

static void options_help(FILE *stream, const maelys_cli_option_t *options,
                         size_t count, size_t width) {
    size_t column = 0u;
    char label[256];
    for (size_t i = 0u; i < count; ++i) {
        if (options[i].hidden) continue;
        option_label(&options[i], label, sizeof(label));
        size_t wide = display_width(label, strlen(label));
        if (wide <= HELP_LABEL_MAXIMUM && wide > column) column = wide;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (options[i].hidden) continue;
        char text[1024];
        option_label(&options[i], label, sizeof(label));
        option_text(&options[i], text, sizeof(text));
        help_entry(stream, label, text, column, width, 0);
    }
}

static void command_help_text(
    FILE *stream, const maelys_cli_app_t *app,
    const maelys_cli_command_t *command, size_t width) {
    char *synopsis = maelys_cli_command_synopsis_alloc(command);
    char line[2048];
    (void)snprintf(line, sizeof(line), "%s %s", app->program,
        synopsis ? synopsis : command->pattern);
    free(synopsis);
    (void)fputs("USAGE\n", stream);
    help_paragraph(stream, line, width, 1);
    (void)fputc('\n', stream);
    (void)help_wrap(stream, command->purpose, 0u, 0u, width, 0);
    (void)fputs("\n\n", stream);
    if (command->unavailable) {
        (void)fputs("UNAVAILABLE IN THIS BUILD\n", stream);
        help_paragraph(stream, command->unavailable, width, 0);
        (void)fputc('\n', stream);
    }
    (void)fputs("EFFECT\n  ", stream);
    if (command->apply_effect != MAELYS_CLI_EFFECT_NONE)
        (void)fprintf(stream, "%s by default; %s with --apply\n",
            maelys_cli_effect_name(command->effect),
            maelys_cli_effect_name(command->apply_effect));
    else
        (void)fprintf(stream, "%s\n", maelys_cli_effect_name(command->effect));
    (void)snprintf(line, sizeof(line), "%s%s%s%s",
        maelys_cli_output_mode_name(command->output),
        command->protocol ? " owned by protocol " : "",
        command->protocol ? command->protocol : "",
        command->delegate ? " (arguments are passed to an external program)" : "");
    (void)fputs("\nOUTPUT\n", stream);
    help_paragraph(stream, line, width, 0);
    if (command->operand_count) {
        size_t column = 0u;
        for (size_t i = 0u; i < command->operand_count; ++i) {
            size_t wide = display_width(command->operands[i].name,
                strlen(command->operands[i].name));
            if (wide <= HELP_LABEL_MAXIMUM && wide > column) column = wide;
        }
        (void)fputs("\nOPERANDS\n", stream);
        for (size_t i = 0u; i < command->operand_count; ++i) {
            const maelys_cli_operand_t *operand = &command->operands[i];
            (void)snprintf(line, sizeof(line), "%s%s", operand->summary,
                operand->required ? "" : " Optional.");
            help_entry(stream, operand->name, line, column, width, 0);
        }
    }
    size_t shown = 0u;
    for (size_t i = 0u; i < command->option_count; ++i)
        if (!command->options[i].hidden) ++shown;
    if (shown) {
        (void)fputs("\nOPTIONS\n", stream);
        options_help(stream, command->options, command->option_count, width);
    }
    (void)snprintf(line, sizeof(line), "Run '%s help conventions' for --format, "
        "--json, --compact, --non-interactive, --color and the others.", app->program);
    (void)fputs("\nGLOBAL OPTIONS\n", stream);
    help_paragraph(stream, line, width, 0);
}

/* 1 when the command is shown and belongs to the family: its identifier is
 * the prefix or lies under it (`note` holds `note.write`), which is the
 * namespace `describe --summary --prefix` selects; or, for `words`, its
 * pattern starts with those words and has more. */
static int in_family(const maelys_cli_command_t *command, const char *prefix,
                     char *const words[], size_t word_count) {
    if (command->hidden) return 0;
    if (prefix) {
        size_t length = strlen(prefix);
        return !strncmp(command->id, prefix, length) &&
            (command->id[length] == '\0' || command->id[length] == '.');
    }
    const char *cursor = command->pattern;
    for (size_t w = 0u; w < word_count; ++w) {
        const char *end = strchr(cursor, ' ');
        if (!end) return 0;
        size_t length = (size_t)(end - cursor);
        if (strlen(words[w]) != length || memcmp(words[w], cursor, length) != 0)
            return 0;
        cursor = end + 1;
    }
    return 1;
}

/* The help of a family: each of its commands with its usage, the purpose
 * below. */
static void family_help_text(
    FILE *stream, const maelys_cli_app_t *app, const char *prefix,
    char *const words[], size_t word_count, size_t width) {
    char line[2048];
    size_t used = (size_t)snprintf(line, sizeof(line), "%s", app->program);
    if (prefix) used += (size_t)snprintf(line + used, sizeof(line) - used, " %s", prefix);
    for (size_t w = 0u; !prefix && w < word_count && used < sizeof(line); ++w)
        used += (size_t)snprintf(line + used, sizeof(line) - used, " %s", words[w]);
    (void)fprintf(stream, "%s - commands\n\nCOMMANDS\n", line);
    size_t count = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        if (!in_family(command, prefix, words, word_count)) continue;
        char *synopsis = maelys_cli_command_synopsis_alloc(command);
        char text[1024];
        (void)snprintf(text, sizeof(text), "%s%s", command->purpose,
            command->unavailable ? " (unavailable in this build)" : "");
        /* Never beside: a usage is as long as it needs to be. */
        help_entry(stream, synopsis ? synopsis : command->pattern, text, 0u, width, 1);
        free(synopsis);
    }
    (void)snprintf(line, sizeof(line), "Run '%s help COMMAND_ID' or '%s COMMAND "
        "--help' for the operands and options of one command.", app->program,
        app->program);
    (void)fputc('\n', stream);
    (void)help_wrap(stream, line, 0u, 0u, width, 0);
    (void)fputc('\n', stream);
}

static void catalog_help_text(FILE *stream, const maelys_cli_app_t *app, size_t width) {
    char line[2048];
    size_t used = (size_t)snprintf(line, sizeof(line), "%s %s", app->program, app->version);
    if (app->summary && *app->summary && used < sizeof(line))
        (void)snprintf(line + used, sizeof(line) - used, " - %s", app->summary);
    (void)help_wrap(stream, line, 0u, 0u, width, 0);
    (void)fprintf(stream, "\n\nUSAGE\n  %s COMMAND [OPERANDS] [OPTIONS]\n", app->program);
    (void)snprintf(line, sizeof(line), "%s help conventions", app->program);
    size_t usage_column = display_width(line, strlen(line));
    (void)snprintf(line, sizeof(line), "%s help COMMAND_ID", app->program);
    help_entry(stream, line, "the operands and options of one command",
        usage_column, width, 0);
    (void)snprintf(line, sizeof(line), "%s help FAMILY", app->program);
    help_entry(stream, line, "the commands of one family, with their usage",
        usage_column, width, 0);
    (void)snprintf(line, sizeof(line), "%s help conventions", app->program);
    help_entry(stream, line, "the options every command takes, and the agent contract",
        usage_column, width, 0);
    (void)fputs("\nCOMMANDS\n", stream);
    size_t count = maelys_cli_app_command_count(app);
    size_t builtin_count = 0u;
    (void)maelys_cli_builtin_commands(&builtin_count);
    size_t column = 0u;
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        size_t wide = display_width(command->pattern, strlen(command->pattern));
        if (!command->hidden && wide <= HELP_LABEL_MAXIMUM && wide > column) column = wide;
    }
    /* The product's commands first, then the ones every program has. */
    for (int built_in = 0; built_in < 2; ++built_in) {
        int any = 0;
        for (size_t i = 0u; i < count; ++i) {
            const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
            if (command->hidden || (i < builtin_count) != (built_in == 1)) continue;
            if (built_in && !any && builtin_count < count) (void)fputc('\n', stream);
            any = 1;
            (void)snprintf(line, sizeof(line), "%s%s", command->purpose,
                command->unavailable ? " (unavailable in this build)" : "");
            help_entry(stream, command->pattern, line, column, width, 0);
        }
    }
    /* What every program has in common is named, not repeated: the options
     * and the contract are the same in every product, and spelled out they
     * were two thirds of this screen. `help conventions` has them whole. */
    size_t transport_count = 0u;
    const maelys_cli_option_t *transport = maelys_cli_transport_options(
        &transport_count);
    size_t used_names = 0u;
    line[0] = '\0';
    for (size_t i = 0u; i < transport_count && used_names < sizeof(line); ++i)
        if (!transport[i].hidden)
            used_names += (size_t)snprintf(line + used_names, sizeof(line) - used_names,
                "%s--%s", used_names ? ", " : "", transport[i].name);
    if (used_names < sizeof(line))
        (void)snprintf(line + used_names, sizeof(line) - used_names,
            ". Run '%s help conventions' for what each one does.", app->program);
    (void)fputs("\nGLOBAL OPTIONS\n", stream);
    help_paragraph(stream, line, width, 0);
    (void)snprintf(line, sizeof(line),
        "Use --format json --non-interactive, and run '%s describe --summary "
        "--format json' first. '%s help conventions' has the rest of the "
        "contract.", app->program, app->program);
    (void)fputs("\nAGENT CONTRACT\n", stream);
    help_paragraph(stream, line, width, 0);
    if (app->agent_guidance && *app->agent_guidance)
        (void)fprintf(stream, "\n%s%s", app->agent_guidance,
            app->agent_guidance[strlen(app->agent_guidance) - 1u] == '\n' ?
            "" : "\n");
}

/* What every program built on the framework has in common: the options
 * every command takes and the contract an agent relies on, whole. */
static void conventions_help_text(FILE *stream, const maelys_cli_app_t *app, size_t width) {
    char line[2048];
    (void)fprintf(stream, "%s - conventions\n\nGLOBAL OPTIONS\n", app->program);
    size_t transport_count = 0u;
    const maelys_cli_option_t *transport = maelys_cli_transport_options(
        &transport_count);
    options_help(stream, transport, transport_count, width);
    (void)snprintf(line, sizeof(line),
        "Use --format json --non-interactive. Run '%s describe --summary "
        "--format json' first, then '%s describe COMMAND_ID --format json' "
        "for the exact input and output contract. Exit 0 is success, 1 is "
        "execution failure, and 2 is a completed validation report with "
        "violations. Transactions plan by default and require --apply. "
        "Stream commands reserve stdout for their protocol. Success data is "
        "written to stdout only; diagnostics and failures go to stderr.",
        app->program, app->program);
    (void)fputs("\nAGENT CONTRACT\n", stream);
    help_paragraph(stream, line, width, 0);
}

#define HELP_CONVENTIONS "conventions"

static int family_exists(const maelys_cli_app_t *app, const char *prefix,
                         char *const words[], size_t word_count);

/* Renders the general help, the help of `target`, or the help of the family
 * `prefix` or `words` names, and replies with data.text and data.commands,
 * the identifiers shown (spec 2.3, section 6). */
static int help_reply(
    maelys_cli_context_t *context, const maelys_cli_command_t *target,
    const char *prefix, char *const words[], size_t word_count) {
    char *text = NULL;
    size_t size = 0u;
    /* The topic is asked for by its name where a family would be; it holds
     * no command, so data.commands is empty. */
    int conventions = !target && prefix && word_count == 0u &&
        !strcmp(prefix, HELP_CONVENTIONS) &&
        !family_exists(context->app, prefix, NULL, 0u);
    int family = !conventions && (prefix || word_count > 0u);
    size_t width = help_width(context);
    FILE *memory = open_memstream(&text, &size);
    if (!memory) return maelys_cli_fail_errno(context,
        MAELYS_CLI_CODE_UNEXPECTED, errno, "help buffer");
    if (target) command_help_text(memory, context->app, target, width);
    else if (conventions) conventions_help_text(memory, context->app, width);
    else if (family) family_help_text(memory, context->app, prefix, words, word_count, width);
    else catalog_help_text(memory, context->app, width);
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
        if (target) built = maelys_cli_json_string(&writer, target->id) == 0;
        else {
            size_t count = maelys_cli_app_command_count(context->app);
            for (size_t i = 0u; built && i < count; ++i) {
                const maelys_cli_command_t *command =
                    maelys_cli_app_command_at(context->app, i);
                if (command->hidden || conventions ||
                    (family && !in_family(command, prefix, words, word_count)))
                    continue;
                built = maelys_cli_json_string(&writer, command->id) == 0;
            }
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

static int help_for(
    maelys_cli_context_t *context, const maelys_cli_command_t *target) {
    return help_reply(context, target, NULL, NULL, 0u);
}

/* 1 when at least one shown command belongs to the family. */
static int family_exists(const maelys_cli_app_t *app, const char *prefix,
                         char *const words[], size_t word_count) {
    size_t count = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < count; ++i)
        if (in_family(maelys_cli_app_command_at(app, i), prefix, words, word_count))
            return 1;
    return 0;
}

static int builtin_help(maelys_cli_context_t *context) {
    const char *query = maelys_cli_operand(context, 0u);
    if (!query) return help_for(context, NULL);
    const maelys_cli_command_t *target = maelys_cli_app_find_command(context->app, query);
    if (target) return help_for(context, target);
    /* Not a command: a family, the namespace describe --prefix selects; or
     * the one topic, which a command or a family of that name would hide. */
    if (family_exists(context->app, query, NULL, 0u) || !strcmp(query, HELP_CONVENTIONS))
        return help_reply(context, NULL, query, NULL, 0u);
    return maelys_cli_fail(context, MAELYS_CLI_CODE_INVALID_COMMAND,
        "Run 'help' without operands to list the commands and their families.",
        "Unknown command identifier or family: %s.", query);
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

int maelys_cli_resolve_helper(
    const maelys_cli_context_t *context, const char *name, char *out_path,
    size_t out_size) {
    if (!context || !context->app || !name || !out_path) {
        errno = EINVAL;
        return -1;
    }
    char executable_directory[PATH_MAX];
    char libexec_program[PATH_MAX];
    char libexec[PATH_MAX];
    const char *directories[3 + 16];
    size_t count = 0u;
    const char *argv0 = context->executable ? context->executable : maelys_cli_argv0;
    if (maelys_cli_executable_directory(argv0, executable_directory,
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
    return maelys_cli_process_resolve(name, directories, count, out_path, out_size);
}

/* Finds the delegate and says nothing: completion asks where it is and has
 * no failure to report when it is not there. Returns 0 with the path; -1
 * otherwise, with errno and, for an absolute delegate that is unusable, the
 * explanation. */
static int locate_delegate(
    maelys_cli_context_t *context, const maelys_cli_command_t *command,
    char *out_path, size_t out_size, const char **out_explanation) {
    const char *delegate = command->delegate;
    if (out_explanation) *out_explanation = NULL;
    if (delegate[0] != '/') {
        return maelys_cli_resolve_helper(context, delegate, out_path, out_size);
    }
    const char *explanation = NULL;
    if (maelys_cli_process_check_executable(delegate, &explanation) != 0) {
        if (out_explanation) *out_explanation = explanation;
        return -1;
    }
    if (strlen(delegate) >= out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out_path, delegate, strlen(delegate) + 1u);
    return 0;
}

static int resolve_delegate(
    maelys_cli_context_t *context, const maelys_cli_command_t *command,
    char *out_path, size_t out_size) {
    const char *explanation = NULL;
    if (locate_delegate(context, command, out_path, out_size, &explanation) == 0)
        return 0;
    int saved = errno;
    if (command->delegate[0] != '/')
        (void)maelys_cli_fail(context, MAELYS_CLI_CODE_NOT_FOUND,
            "Install the optional component that provides this command.",
            "External command '%s' for '%s' is not installed.",
            command->delegate, command->id);
    else if (saved != ENAMETOOLONG)
        (void)maelys_cli_fail(context, MAELYS_CLI_CODE_PROCESS_FAILED,
            "Install the external command with safe ownership and modes.",
            "External command %s is unusable: %s.", command->delegate,
            explanation ? explanation : strerror(saved));
    return -1;
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

static void apply_environment_format(maelys_cli_invocation_t *invocation) {
    const char *format = getenv("MAELYS_CLI_FORMAT");
    if (!format || !*format) return;
    if (!strcmp(format, "json")) invocation->format = MAELYS_CLI_FORMAT_JSON;
    else if (!strcmp(format, "text")) invocation->format = MAELYS_CLI_FORMAT_TEXT;
}

static void prescan_rendering(
    int argc, char **argv, maelys_cli_invocation_t *invocation) {
    apply_environment_format(invocation);
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
    context.executable = maelys_cli_argv0;
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
        /* `PROGRAM note --help`: the words name no command, but a family of
         * them, and help was asked for: the help of that family, as
         * `PROGRAM help note` gives it, rather than "unknown command". */
        char *family[MAELYS_CLI_MAX_OPERANDS];
        size_t family_count = 0u;
        int help_asked = 0;
        for (int i = 0; argv && i < argc && strcmp(argv[i], "--") != 0; ++i) {
            if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) help_asked = 1;
            else if (argv[i][0] != '-' && (size_t)i == family_count &&
                     family_count < MAELYS_CLI_MAX_OPERANDS)
                family[family_count++] = argv[i];
        }
        if (help_asked && family_count > 0u &&
            !strcmp(error.code, MAELYS_CLI_CODE_INVALID_COMMAND) &&
            family_exists(app, NULL, family, family_count)) {
            invocation.command = maelys_cli_app_find_command(app, "help");
            int shown = help_reply(&context, NULL, NULL, family, family_count);
            maelys_cli_json_writer_clear(&context.records);
            (void)fflush(context.out);
            (void)fflush(context.err);
            return shown;
        }
        (void)maelys_cli_fail_error(&context, &error);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    /* MAELYS_CLI_FORMAT is the default format: --format and --json override
     * it, --compact and --pretty select none and leave it in force. */
    if (!invocation.format_requested) apply_environment_format(&invocation);
    maelys_cli_terminal_detect(&context.terminal, invocation.color);
    const maelys_cli_command_t *command = invocation.command;
    if (pager_applies(&context)) start_pager(&context);
    int result;
    if (invocation.help_requested && !command->delegate) {
        /* Command-level help renders through the help builtin's contract. */
        result = help_for(&context, command);
    } else if (command->delegate) {
        result = delegate_command(&context, command);
    } else if (!command->handler) {
        /* The code says why, since UNSUPPORTED means absence and a command
         * can be unavailable for a cause that is not absence. */
        result = maelys_cli_fail(&context,
            command->unavailable_code ? command->unavailable_code :
                MAELYS_CLI_CODE_UNSUPPORTED,
            "Use another command or install the component providing it.",
            "'%s' is not available in this build: %s", command->id,
            command->unavailable ? command->unavailable : "no implementation");
    } else if (field_refused(&invocation, &error)) {
        /* After an unavailable command has said why it cannot run, before
         * the handler: a rendering refusal never follows a write. */
        result = maelys_cli_fail_error(&context, &error);
    } else {
        result = command->handler(&context);
        if (!context.replied && command->output != MAELYS_CLI_OUTPUT_STREAM) {
            result = maelys_cli_fail(&context, MAELYS_CLI_CODE_UNEXPECTED,
                "Report this defect to the command implementation.",
                "Command '%s' finished without reporting a result.", command->id);
        }
    }
    maelys_cli_progress_done(&context);
    maelys_cli_json_writer_clear(&context.records);
    (void)fflush(context.out);
    finish_pager(&context);
    (void)fflush(context.err);
    return result;
}

int maelys_cli_main(const maelys_cli_app_t *app, int argc, char **argv) {
    if (argc > 0 && argv && argv[0]) maelys_cli_argv0 = argv[0];
    return maelys_cli_run(app, argc > 0 ? argc - 1 : 0,
        argc > 0 ? argv + 1 : NULL, stdout, stderr);
}
