#include "maelys/cli/invocation.h"
#include "maelys/cli/values.h"
#include "internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void maelys_cli_error_set(
    maelys_cli_error_t *error, const char *code, const char *hint,
    const char *format, ...) {
    if (!error) return;
    memset(error, 0, sizeof(*error));
    (void)snprintf(error->code, sizeof(error->code), "%s",
        code ? code : MAELYS_CLI_CODE_UNEXPECTED);
    if (hint) (void)snprintf(error->hint, sizeof(error->hint), "%s", hint);
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error->message, sizeof(error->message), format, arguments);
    va_end(arguments);
}

void maelys_cli_error_from_errno(
    maelys_cli_error_t *error, const char *code, int saved_errno,
    const char *what) {
    maelys_cli_error_set(error, code,
        "Inspect the named path or resource, correct its state and retry.",
        "%s: %s", what ? what : "operation failed", strerror(saved_errno));
}

static const char *const format_choices[] = {"text", "json", "jsonl", NULL};
static const char *const color_choices[] = {"auto", "always", "never", NULL};

static const maelys_cli_option_t transport[] = {
    {MAELYS_CLI_CHOICE("format",
     "Select text for humans, json for one envelope or jsonl for records.",
     format_choices), .default_text = "text"},
    {MAELYS_CLI_FLAG("json", "Exact alias of --format json.")},
    {MAELYS_CLI_FLAG("compact", "Render JSON on a single line.")},
    {MAELYS_CLI_FLAG("pretty", "--pretty=false selects compact JSON.")},
    {MAELYS_CLI_FLAG("non-interactive",
     "Never prompt; fail instead of asking a question.")},
    {MAELYS_CLI_CHOICE("color", "Control ANSI colors on terminals.",
     color_choices), .default_text = "auto"},
    {MAELYS_CLI_FLAG("help", "Show the help of the selected command.")},
};

const maelys_cli_option_t *maelys_cli_transport_options(size_t *out_count) {
    if (out_count) *out_count = MAELYS_CLI_COUNT(transport);
    return transport;
}

size_t maelys_cli_pattern_words(const char *pattern) {
    size_t count = 0u;
    int in_word = 0;
    for (const char *p = pattern ? pattern : ""; *p; ++p) {
        if (*p == ' ') in_word = 0;
        else if (!in_word) {
            ++count;
            in_word = 1;
        }
    }
    return count;
}

static int pattern_matches(const char *pattern, int argc, char **argv) {
    const char *cursor = pattern;
    int index = 0;
    while (*cursor) {
        const char *end = strchr(cursor, ' ');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (index >= argc || strlen(argv[index]) != length ||
            memcmp(argv[index], cursor, length) != 0)
            return 0;
        ++index;
        if (!end) break;
        cursor = end + 1;
    }
    return 1;
}

static const maelys_cli_command_t *resolve_command(
    const maelys_cli_app_t *app, int argc, char **argv, int *out_words) {
    *out_words = 0;
    if (argc == 0) return maelys_cli_app_find_command(app, "help");
    if (!strcmp(argv[0], "--help") || !strcmp(argv[0], "-h")) {
        *out_words = 1;
        return maelys_cli_app_find_command(app, "help");
    }
    if (!strcmp(argv[0], "--version")) {
        *out_words = 1;
        return maelys_cli_app_find_command(app, "version");
    }
    const maelys_cli_command_t *best = NULL;
    size_t best_words = 0u;
    size_t count = maelys_cli_app_command_count(app);
    for (size_t i = 0u; i < count; ++i) {
        const maelys_cli_command_t *command = maelys_cli_app_command_at(app, i);
        size_t words = maelys_cli_pattern_words(command->pattern);
        if (words > best_words && pattern_matches(command->pattern, argc, argv)) {
            best = command;
            best_words = words;
        }
    }
    *out_words = (int)best_words;
    return best;
}

static const maelys_cli_option_t *find_option(
    const maelys_cli_option_t *options, size_t count, const char *name) {
    for (size_t i = 0u; i < count; ++i)
        if (!strcmp(options[i].name, name)) return &options[i];
    return NULL;
}

static maelys_cli_parsed_option_t *find_parsed(
    maelys_cli_invocation_t *invocation, const char *name) {
    for (size_t i = 0u; i < invocation->option_count; ++i)
        if (!strcmp(invocation->options[i].name, name))
            return &invocation->options[i];
    return NULL;
}

static int option_enabled(
    const maelys_cli_invocation_t *invocation, const char *name) {
    for (size_t i = 0u; i < invocation->option_count; ++i)
        if (!strcmp(invocation->options[i].name, name) &&
            invocation->options[i].boolean_value)
            return 1;
    return 0;
}

static uint64_t unsigned_maximum(const maelys_cli_option_t *option) {
    return option->maximum ? option->maximum : UINT64_MAX;
}

/* Public so the runtime can derive typed defaults from default_text. */
int maelys_cli_option_validate_text(
    const maelys_cli_option_t *option, const char *value,
    maelys_cli_parsed_option_t *parsed, maelys_cli_error_t *error);

/* Operands reuse the option validator through a synthetic descriptor. */
static maelys_cli_option_t operand_as_option(const maelys_cli_operand_t *operand) {
    maelys_cli_option_t option;
    memset(&option, 0, sizeof(option));
    option.name = operand->name;
    option.kind = operand->kind;
    option.choices = operand->choices;
    option.minimum = operand->minimum;
    option.maximum = operand->maximum;
    option.signed_minimum = operand->signed_minimum;
    option.signed_maximum = operand->signed_maximum;
    option.hex_digits = operand->hex_digits;
    option.hex_digits_alternative = operand->hex_digits_alternative;
    return option;
}

static int validate_value(
    const maelys_cli_option_t *option, const char *value,
    maelys_cli_parsed_option_t *parsed, maelys_cli_error_t *error);

int maelys_cli_option_validate_text(
    const maelys_cli_option_t *option, const char *value,
    maelys_cli_parsed_option_t *parsed, maelys_cli_error_t *error) {
    return validate_value(option, value, parsed, error);
}

static int validate_value(
    const maelys_cli_option_t *option, const char *value,
    maelys_cli_parsed_option_t *parsed, maelys_cli_error_t *error) {
    static const char *hint = "Correct the stated option value and retry.";
    switch (option->kind) {
        case MAELYS_CLI_VALUE_NONE:
            return 0;
        case MAELYS_CLI_VALUE_ABSOLUTE_PATH:
            if (value[0] != '/') {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects an absolute path.", option->name);
                return -1;
            }
            return 0;
        case MAELYS_CLI_VALUE_DIGEST: {
            const char *colon = strchr(value, ':');
            size_t algorithm_length = colon ? (size_t)(colon - value) : 0u;
            size_t index = 0u;
            size_t digits = 0u;
            for (; option->choices && option->choices[index]; ++index) {
                if (strlen(option->choices[index]) == algorithm_length &&
                    memcmp(option->choices[index], value, algorithm_length) == 0) {
                    digits = maelys_cli_digest_hex_digits(option->choices[index]);
                    break;
                }
            }
            if (!colon || digits == 0u ||
                maelys_cli_parse_hex(colon + 1, digits) != 0) {
                char allowed[256];
                size_t used = 0u;
                allowed[0] = '\0';
                for (size_t i = 0u; option->choices && option->choices[i]; ++i) {
                    int written = snprintf(allowed + used,
                        sizeof(allowed) - used, "%s%s:%zu", i ? ", " : "",
                        option->choices[i],
                        maelys_cli_digest_hex_digits(option->choices[i]));
                    if (written < 0 || (size_t)written >= sizeof(allowed) - used)
                        break;
                    used += (size_t)written;
                }
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects ALGORITHM:HEX with lowercase "
                    "hexadecimal digits (%s).", option->name, allowed);
                return -1;
            }
            parsed->choice_index = index;
            return 0;
        }
        case MAELYS_CLI_VALUE_STRING:
        case MAELYS_CLI_VALUE_PATH:
            if (!*value) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s requires a non-empty value.",
                    option->name);
                return -1;
            }
            return 0;
        case MAELYS_CLI_VALUE_INTEGER: {
            int64_t minimum = option->signed_minimum;
            int64_t maximum = option->signed_maximum;
            if (minimum == 0 && maximum == 0) {
                minimum = INT64_MIN;
                maximum = INT64_MAX;
            }
            if (maelys_cli_parse_i64_decimal(value, minimum, maximum,
                    &parsed->signed_value) != 0) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects an integer between %" PRId64
                    " and %" PRId64 ".", option->name, minimum, maximum);
                return -1;
            }
            return 0;
        }
        case MAELYS_CLI_VALUE_UNSIGNED:
            if (maelys_cli_parse_u64_decimal(value, option->minimum,
                    unsigned_maximum(option), &parsed->unsigned_value) != 0) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects an unsigned integer between %"
                    PRIu64 " and %" PRIu64 ".", option->name, option->minimum,
                    unsigned_maximum(option));
                return -1;
            }
            return 0;
        case MAELYS_CLI_VALUE_SIZE:
            if (maelys_cli_parse_byte_size(value, option->minimum,
                    unsigned_maximum(option), &parsed->unsigned_value) != 0) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects a byte size between %" PRIu64
                    " and %" PRIu64 " (K, M, G or T suffix accepted).",
                    option->name, option->minimum, unsigned_maximum(option));
                return -1;
            }
            return 0;
        case MAELYS_CLI_VALUE_DURATION:
            if (maelys_cli_parse_duration_ms(value, option->minimum,
                    unsigned_maximum(option), &parsed->unsigned_value) != 0) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects a duration with a unit "
                    "(ms, s, m, h or d) between %" PRIu64 " and %" PRIu64
                    " milliseconds.", option->name, option->minimum,
                    unsigned_maximum(option));
                return -1;
            }
            return 0;
        case MAELYS_CLI_VALUE_CHOICE:
            if (!option->choices || maelys_cli_parse_choice(value,
                    option->choices, &parsed->choice_index) != 0) {
                char allowed[256];
                size_t used = 0u;
                allowed[0] = '\0';
                for (size_t i = 0u; option->choices && option->choices[i]; ++i) {
                    int written = snprintf(allowed + used,
                        sizeof(allowed) - used, "%s%s", i ? ", " : "",
                        option->choices[i]);
                    if (written < 0 || (size_t)written >= sizeof(allowed) - used)
                        break;
                    used += (size_t)written;
                }
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    hint, "Option --%s expects one of: %s.", option->name,
                    allowed);
                return -1;
            }
            return 0;
        case MAELYS_CLI_VALUE_HEX:
            if (maelys_cli_parse_hex(value, option->hex_digits) != 0 &&
                (option->hex_digits_alternative == 0u ||
                 maelys_cli_parse_hex(value, option->hex_digits_alternative) != 0)) {
                if (option->hex_digits_alternative)
                    maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                        hint, "Option --%s expects %zu or %zu lowercase "
                        "hexadecimal digits.", option->name, option->hex_digits,
                        option->hex_digits_alternative);
                else
                    maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                        hint, "Option --%s expects %zu lowercase hexadecimal "
                        "digits.", option->name, option->hex_digits);
                return -1;
            }
            return 0;
    }
    maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, NULL,
        "Option --%s has an unknown value kind.", option->name);
    return -1;
}

static void apply_transport(
    maelys_cli_invocation_t *out, const maelys_cli_parsed_option_t *parsed) {
    const char *name = parsed->name;
    if (!strcmp(name, "format")) {
        out->format = (maelys_cli_format_t)parsed->choice_index;
        out->rendering_requested = 1;
    } else if (!strcmp(name, "json")) {
        out->format = parsed->boolean_value ? MAELYS_CLI_FORMAT_JSON :
            MAELYS_CLI_FORMAT_TEXT;
        out->rendering_requested = 1;
    } else if (!strcmp(name, "compact")) {
        out->compact = parsed->boolean_value;
        out->rendering_requested = 1;
    } else if (!strcmp(name, "pretty")) {
        out->compact = !parsed->boolean_value;
        out->rendering_requested = 1;
    } else if (!strcmp(name, "non-interactive")) {
        out->non_interactive = parsed->boolean_value;
    } else if (!strcmp(name, "color")) {
        out->color = (maelys_cli_color_mode_t)parsed->choice_index;
    } else if (!strcmp(name, "help")) {
        out->help_requested = parsed->boolean_value;
    }
}

static int add_operand(
    maelys_cli_invocation_t *out, const char *value, maelys_cli_error_t *error) {
    if (out->operand_count >= MAELYS_CLI_MAX_OPERANDS) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Reduce the number of operands and retry.",
            "Too many operands for '%s'.", out->command->id);
        return -1;
    }
    out->operands[out->operand_count++] = value;
    return 0;
}

int maelys_cli_parse(
    const maelys_cli_app_t *app, int argc, char **argv,
    maelys_cli_invocation_t *out, maelys_cli_error_t *error) {
    if (!app || argc < 0 || (argc > 0 && !argv) || !out) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Invalid parser arguments.");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (error) memset(error, 0, sizeof(*error));
    int words = 0;
    out->command = resolve_command(app, argc, argv, &words);
    if (!out->command) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_INVALID_COMMAND,
            "Run 'describe --summary --format json' to discover valid "
            "command identifiers and usage.",
            "Unknown %s command: %s.", app->program, argc ? argv[0] : "");
        return -1;
    }
    out->pattern_words = words;
    char *synopsis_text = maelys_cli_command_synopsis_alloc(out->command);
    const char *synopsis = synopsis_text ? synopsis_text : out->command->pattern;
#define PARSE_FAIL() do { free(synopsis_text); return -1; } while (0)
#define PARSE_DONE() do { free(synopsis_text); return 0; } while (0)

    if (out->command->delegate) {
        /* Everything after the pattern belongs to the external program. */
        for (int i = words; i < argc; ++i)
            if (add_operand(out, argv[i], error) != 0) PARSE_FAIL();
        PARSE_DONE();
    }

    int passthrough = 0;
    for (int i = words; i < argc; ++i) {
        const char *argument = argv[i];
        if (passthrough || strncmp(argument, "--", 2u) != 0 || !argument[2]) {
            if (!passthrough && !strcmp(argument, "--")) {
                passthrough = 1;
                continue;
            }
            if (add_operand(out, argument, error) != 0) PARSE_FAIL();
            continue;
        }
        const char *name = argument + 2;
        const char *equals = strchr(name, '=');
        char option_name[MAELYS_CLI_MAX_OPTION_NAME];
        size_t name_length = equals ? (size_t)(equals - name) : strlen(name);
        if (name_length == 0u || name_length >= sizeof(option_name)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Correct the stated option and retry.",
                "Invalid option spelling: %s.", argument);
            PARSE_FAIL();
        }
        memcpy(option_name, name, name_length);
        option_name[name_length] = '\0';

        const maelys_cli_option_t *descriptor = find_option(
            out->command->options, out->command->option_count, option_name);
        int is_transport = 0;
        if (!descriptor) {
            descriptor = find_option(transport, MAELYS_CLI_COUNT(transport),
                option_name);
            is_transport = descriptor != NULL;
        }
        if (!descriptor) {
            if ((!strcmp(option_name, "dry-run") || !strcmp(option_name, "plan")) &&
                find_option(out->command->options, out->command->option_count,
                    "apply")) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    "Remove the legacy flag for a plan. Add --apply only "
                    "after reviewing that plan.",
                    "--%s is not supported; planning is the default for "
                    "transactional commands.", option_name);
                PARSE_FAIL();
            }
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Run describe for this command and use only its declared "
                "options.",
                "Option --%s is not supported by '%s'. Use '%s'.",
                option_name, out->command->id, synopsis);
            PARSE_FAIL();
        }
        if (out->option_count >= MAELYS_CLI_MAX_OPTIONS) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Reduce the number of options and retry.",
                "Too many options for '%s'.", out->command->id);
            PARSE_FAIL();
        }
        if (find_parsed(out, descriptor->name) && !descriptor->repeatable) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Remove the duplicate option and retry.",
                "Option --%s may be supplied only once.", descriptor->name);
            PARSE_FAIL();
        }
        maelys_cli_parsed_option_t parsed;
        memset(&parsed, 0, sizeof(parsed));
        parsed.descriptor = descriptor;
        parsed.transport = is_transport;
        parsed.name = descriptor->name;
        parsed.boolean_value = 1;
        const char *value = equals ? equals + 1 : NULL;
        if (descriptor->kind == MAELYS_CLI_VALUE_NONE) {
            if (value && maelys_cli_parse_boolean(value, &parsed.boolean_value) != 0) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    "Use true or false for an explicit boolean.",
                    "Option --%s expects a boolean.", descriptor->name);
                PARSE_FAIL();
            }
        } else {
            if (!value) {
                if (i + 1 >= argc) {
                    maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                        "Supply the documented option value and retry.",
                        "Option --%s requires a value.", descriptor->name);
                    PARSE_FAIL();
                }
                value = argv[++i];
            }
            parsed.value = value;
            if (validate_value(descriptor, value, &parsed, error) != 0) PARSE_FAIL();
        }
        out->options[out->option_count++] = parsed;
        if (is_transport) apply_transport(out, &parsed);
    }
    if (out->help_requested) PARSE_DONE();

    for (size_t i = 0u; i < out->option_count; ++i) {
        const maelys_cli_option_t *option = out->options[i].descriptor;
        if (out->options[i].transport || !out->options[i].boolean_value) continue;
        if (option->depends_on && !option_enabled(out, option->depends_on)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Supply the dependent option and retry.",
                "--%s requires --%s.", option->name, option->depends_on);
            PARSE_FAIL();
        }
        if (option->conflicts_with && option_enabled(out, option->conflicts_with)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Remove one of the conflicting options and retry.",
                "--%s conflicts with --%s.", option->name, option->conflicts_with);
            PARSE_FAIL();
        }
        for (size_t j = 0u; option->conflicts_with && j < out->command->operand_count; ++j) {
            if (strcmp(out->command->operands[j].name, option->conflicts_with) != 0)
                continue;
            if (out->operand_count > j) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    "Remove either the option or the operand and retry.",
                    "--%s conflicts with operand %s.", option->name,
                    option->conflicts_with);
                PARSE_FAIL();
            }
        }
        for (size_t d = 0u; option->depends_on_all && option->depends_on_all[d]; ++d) {
            if (!option_enabled(out, option->depends_on_all[d])) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    "Supply every dependent option and retry.",
                    "--%s requires --%s.", option->name, option->depends_on_all[d]);
                PARSE_FAIL();
            }
        }
    }
    for (size_t i = 0u; i < out->command->option_count; ++i) {
        const maelys_cli_option_t *option = &out->command->options[i];
        if (!option->group || !option_enabled(out, option->name)) continue;
        for (size_t j = 0u; j < out->command->option_count; ++j) {
            const maelys_cli_option_t *peer = &out->command->options[j];
            if (peer->group && !strcmp(peer->group, option->group) &&
                !option_enabled(out, peer->name)) {
                maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                    "Supply the whole option group or none of it.",
                    "--%s belongs to group '%s' and requires --%s.",
                    option->name, option->group, peer->name);
                PARSE_FAIL();
            }
        }
    }
    for (size_t i = 0u; i < out->command->option_count; ++i) {
        const maelys_cli_option_t *option = &out->command->options[i];
        if (option->required && !find_parsed(out, option->name)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Supply the required option and retry.",
                "--%s is required by '%s'. Use '%s'.", option->name,
                out->command->id, synopsis);
            PARSE_FAIL();
        }
    }
    size_t required = 0u;
    int variadic = 0;
    for (size_t i = 0u; i < out->command->operand_count; ++i) {
        if (out->command->operands[i].required) ++required;
        if (out->command->operands[i].variadic) variadic = 1;
    }
    if (out->operand_count < required ||
        (!variadic && out->operand_count > out->command->operand_count)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use the synopsis returned by describe and retry.",
            "Operands do not match '%s'. Use '%s'.", out->command->id, synopsis);
        PARSE_FAIL();
    }
    for (size_t i = 0u; i < out->operand_count; ++i) {
        size_t slot = i < out->command->operand_count ? i :
            out->command->operand_count - 1u;
        const maelys_cli_operand_t *operand = &out->command->operands[slot];
        maelys_cli_parsed_option_t *parsed = &out->operand_values[i];
        memset(parsed, 0, sizeof(*parsed));
        parsed->name = operand->name;
        parsed->value = out->operands[i];
        if (operand->kind == MAELYS_CLI_VALUE_NONE) continue;
        maelys_cli_option_t spec = operand_as_option(operand);
        maelys_cli_error_t detail;
        if (!*out->operands[i] ||
            validate_value(&spec, out->operands[i], parsed, &detail) != 0) {
            const char *reason = strstr(detail.message, " expects ");
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Correct the stated operand and retry.",
                "Operand %s%s", operand->name,
                *out->operands[i] && reason ? reason :
                " must not be empty.");
            PARSE_FAIL();
        }
        parsed->descriptor = NULL;
        parsed->boolean_value = 1;
    }
    if (out->command->output == MAELYS_CLI_OUTPUT_STREAM && out->rendering_requested) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Remove rendering flags; stdout is reserved for the declared "
            "protocol stream.",
            "'%s' is a stream command and does not support CLI output "
            "rendering.", out->command->id);
        PARSE_FAIL();
    }
    if (out->format == MAELYS_CLI_FORMAT_JSONL &&
        out->command->output != MAELYS_CLI_OUTPUT_RECORDS) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use --format json for this command; jsonl is reserved for "
            "record streams.",
            "'%s' does not produce records and cannot render jsonl.",
            out->command->id);
        PARSE_FAIL();
    }
    PARSE_DONE();
}

const char *maelys_cli_invocation_operand(
    const maelys_cli_invocation_t *invocation, size_t index) {
    return invocation && index < invocation->operand_count ?
        invocation->operands[index] : NULL;
}

const maelys_cli_parsed_option_t *maelys_cli_invocation_operand_value(
    const maelys_cli_invocation_t *invocation, size_t index) {
    if (!invocation || index >= invocation->operand_count ||
        !invocation->command)
        return NULL;
    size_t slot = index < invocation->command->operand_count ? index :
        invocation->command->operand_count - 1u;
    if (invocation->command->operand_count == 0u ||
        invocation->command->operands[slot].kind == MAELYS_CLI_VALUE_NONE)
        return NULL;
    return &invocation->operand_values[index];
}

const maelys_cli_parsed_option_t *maelys_cli_invocation_option(
    const maelys_cli_invocation_t *invocation, const char *name) {
    return maelys_cli_invocation_option_at(invocation, name, 0u);
}

const maelys_cli_parsed_option_t *maelys_cli_invocation_option_at(
    const maelys_cli_invocation_t *invocation, const char *name,
    size_t occurrence) {
    if (!invocation || !name) return NULL;
    size_t seen = 0u;
    for (size_t i = 0u; i < invocation->option_count; ++i) {
        if (strcmp(invocation->options[i].name, name) != 0) continue;
        if (seen++ == occurrence) return &invocation->options[i];
    }
    return NULL;
}

size_t maelys_cli_invocation_option_count(
    const maelys_cli_invocation_t *invocation, const char *name) {
    if (!invocation || !name) return 0u;
    size_t count = 0u;
    for (size_t i = 0u; i < invocation->option_count; ++i)
        if (!strcmp(invocation->options[i].name, name)) ++count;
    return count;
}
