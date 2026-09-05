#include "maelys/cli/catalog.h"

#include <errno.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char *maelys_cli_value_kind_name(maelys_cli_value_kind_t kind) {
    switch (kind) {
        case MAELYS_CLI_VALUE_NONE: return "boolean";
        case MAELYS_CLI_VALUE_STRING: return "string";
        case MAELYS_CLI_VALUE_INTEGER: return "integer";
        case MAELYS_CLI_VALUE_UNSIGNED: return "unsigned";
        case MAELYS_CLI_VALUE_SIZE: return "size";
        case MAELYS_CLI_VALUE_DURATION: return "duration";
        case MAELYS_CLI_VALUE_PATH: return "path";
        case MAELYS_CLI_VALUE_CHOICE: return "choice";
        case MAELYS_CLI_VALUE_HEX: return "hex";
        case MAELYS_CLI_VALUE_ABSOLUTE_PATH: return "absolute-path";
        case MAELYS_CLI_VALUE_DIGEST: return "digest";
    }
    return "unknown";
}

size_t maelys_cli_digest_hex_digits(const char *algorithm) {
    if (!algorithm) return 0u;
    if (!strcmp(algorithm, "sha1")) return 40u;
    if (!strcmp(algorithm, "sha256")) return 64u;
    if (!strcmp(algorithm, "sha384")) return 96u;
    if (!strcmp(algorithm, "sha512")) return 128u;
    return 0u;
}

const char *maelys_cli_effect_name(maelys_cli_effect_t effect) {
    switch (effect) {
        case MAELYS_CLI_EFFECT_NONE: return "none";
        case MAELYS_CLI_EFFECT_READ: return "read";
        case MAELYS_CLI_EFFECT_PREVIEW: return "preview";
        case MAELYS_CLI_EFFECT_APPLY: return "apply";
        case MAELYS_CLI_EFFECT_COMMIT: return "commit";
        case MAELYS_CLI_EFFECT_EXECUTE: return "execute";
        case MAELYS_CLI_EFFECT_STREAM: return "stream";
    }
    return "unknown";
}

const char *maelys_cli_output_mode_name(maelys_cli_output_mode_t mode) {
    switch (mode) {
        case MAELYS_CLI_OUTPUT_ENVELOPE: return "json-envelope";
        case MAELYS_CLI_OUTPUT_RECORDS: return "json-records";
        case MAELYS_CLI_OUTPUT_STREAM: return "protocol-stream";
    }
    return "unknown";
}

typedef struct sink {
    char *out;
    size_t size;
    size_t used;
    int truncated;
} sink_t;

static void put(sink_t *sink, const char *text) {
    size_t length = strlen(text);
    if (sink->truncated) return;
    if (sink->used + length >= sink->size) {
        sink->truncated = 1;
        return;
    }
    memcpy(sink->out + sink->used, text, length + 1u);
    sink->used += length;
}

static void put_option(sink_t *sink, const maelys_cli_option_t *option) {
    put(sink, " ");
    if (!option->required) put(sink, "[");
    put(sink, "--");
    put(sink, option->name);
    if (option->kind != MAELYS_CLI_VALUE_NONE) {
        put(sink, " ");
        if (option->kind == MAELYS_CLI_VALUE_DIGEST && !option->value_name) {
            put(sink, "ALGORITHM:HEX");
        } else if (option->kind == MAELYS_CLI_VALUE_CHOICE && option->choices &&
            !option->value_name) {
            for (size_t j = 0u; option->choices[j]; ++j) {
                if (j) put(sink, "|");
                put(sink, option->choices[j]);
            }
        } else {
            put(sink, option->value_name ? option->value_name : "VALUE");
        }
    }
    if (option->repeatable) put(sink, "...");
    if (!option->required) put(sink, "]");
}

static void render_synopsis(const maelys_cli_command_t *command, sink_t *sink) {
    put(sink, command->pattern ? command->pattern : "");
    for (size_t i = 0u; i < command->operand_count; ++i) {
        const maelys_cli_operand_t *operand = &command->operands[i];
        put(sink, " ");
        if (!operand->required) put(sink, "[");
        put(sink, operand->name);
        if (operand->variadic) put(sink, "...");
        if (!operand->required) put(sink, "]");
    }
    for (size_t i = 0u; i < command->option_count; ++i)
        if (command->options[i].required && !command->options[i].hidden)
            put_option(sink, &command->options[i]);
    for (size_t i = 0u; i < command->option_count; ++i)
        if (!command->options[i].required && !command->options[i].hidden)
            put_option(sink, &command->options[i]);
}

int maelys_cli_command_synopsis(
    const maelys_cli_command_t *command, char *out, size_t out_size) {
    if (!command || !out || out_size == 0u) return -1;
    if (command->synopsis) {
        size_t length = strlen(command->synopsis);
        if (length >= out_size) return -1;
        memcpy(out, command->synopsis, length + 1u);
        return 0;
    }
    sink_t sink = {out, out_size, 0u, 0};
    out[0] = '\0';
    render_synopsis(command, &sink);
    return sink.truncated ? -1 : 0;
}

char *maelys_cli_command_synopsis_alloc(const maelys_cli_command_t *command) {
    if (!command) return NULL;
    if (command->synopsis) return strdup(command->synopsis);
    char *buffer = malloc(MAELYS_CLI_MAX_SYNOPSIS + 1u);
    if (!buffer) return NULL;
    sink_t sink = {buffer, MAELYS_CLI_MAX_SYNOPSIS + 1u, 0u, 0};
    buffer[0] = '\0';
    render_synopsis(command, &sink);
    if (sink.truncated) {
        free(buffer);
        return NULL;
    }
    char *result = realloc(buffer, sink.used + 1u);
    return result ? result : buffer;
}

int maelys_cli_catalog_concat(
    const maelys_cli_catalog_part_t *parts, size_t part_count,
    maelys_cli_command_t **out_commands, size_t *out_count) {
    if (!out_commands || !out_count || (part_count && !parts)) {
        errno = EINVAL;
        return -1;
    }
    *out_commands = NULL;
    *out_count = 0u;
    size_t total = 0u;
    for (size_t i = 0u; i < part_count; ++i) {
        if (parts[i].count && !parts[i].commands) {
            errno = EINVAL;
            return -1;
        }
        if (total > SIZE_MAX - parts[i].count) {
            errno = EOVERFLOW;
            return -1;
        }
        total += parts[i].count;
    }
    maelys_cli_command_t *commands = calloc(total ? total : 1u, sizeof(*commands));
    if (!commands) {
        errno = ENOMEM;
        return -1;
    }
    size_t count = 0u;
    for (size_t i = 0u; i < part_count; ++i) {
        for (size_t j = 0u; j < parts[i].count; ++j) {
            const maelys_cli_command_t *command = &parts[i].commands[j];
            size_t slot = count;
            for (size_t k = 0u; command->id && k < count; ++k) {
                if (!commands[k].id || strcmp(commands[k].id, command->id) != 0)
                    continue;
                if (!commands[k].unavailable) {
                    /* A provided command is never replaced by a later part. */
                    free(commands);
                    *out_commands = NULL;
                    errno = EEXIST;
                    return -1;
                }
                slot = k;
                break;
            }
            commands[slot] = *command;
            if (slot == count) ++count;
        }
    }
    *out_commands = commands;
    *out_count = count;
    return 0;
}
