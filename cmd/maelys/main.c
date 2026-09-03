/*
 * `maelys`: the dispatcher. Built-in commands manage the framework itself;
 * every other command is an external process declared by an installed
 * manifest and started with execve, never through a shell or PATH lookup.
 */
#include "agents.h"

#include <maelys/cli.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int commands_list(maelys_cli_context_t *context);

static const maelys_cli_operand_t passthrough_operands[] = {
    {MAELYS_CLI_OPERAND_REST("ARGUMENTS",
     "Arguments passed verbatim to the external command.")},
};

#define COMMANDS_LIST_SCHEMA "{\"type\":\"object\",\"additionalProperties\":" \
    "false,\"required\":[\"count\",\"records\"],\"properties\":{\"count\":{" \
    "\"type\":\"integer\",\"minimum\":0},\"records\":{\"type\":\"array\"," \
    "\"items\":{\"type\":\"object\",\"additionalProperties\":false,\"required\":" \
    "[\"command\",\"executable\",\"manifest\",\"version\",\"summary\"," \
    "\"cliApi\",\"digestVerified\"],\"properties\":{\"command\":{\"type\":" \
    "\"string\"},\"executable\":{\"type\":\"string\"},\"manifest\":{\"type\":" \
    "\"string\"},\"version\":{\"type\":\"string\"},\"summary\":{\"type\":" \
    "\"string\"},\"cliApi\":{\"type\":\"integer\"},\"digestVerified\":{" \
    "\"type\":\"boolean\"}}}}}}"

static const maelys_cli_command_t builtin_commands[] = {
    {MAELYS_CLI_RECORDS("commands.list", "commands list",
     "List the external commands declared by installed manifests.",
     commands_list), MAELYS_CLI_SCHEMA(COMMANDS_LIST_SCHEMA)},
    {MAELYS_CLI_TRANSACTION("agents.install", "agents install",
     "Install or refresh the maelys-cli agent instructions of a project.",
     maelys_agents_install),
     .operands = maelys_agents_operands, .operand_count = 1u,
     .options = maelys_agents_install_options, .option_count = 2u,
     MAELYS_CLI_SCHEMA(maelys_agents_install_schema)},
    {MAELYS_CLI_READ("agents.status", "agents status",
     "Report whether a project's maelys-cli agent instructions are current.",
     maelys_agents_status),
     .operands = maelys_agents_operands, .operand_count = 1u,
     .options = maelys_agents_status_options, .option_count = 1u,
     MAELYS_CLI_SCHEMA(maelys_agents_status_schema)},
};

typedef struct dispatcher_state {
    maelys_cli_extension_set_t extensions;
    maelys_cli_command_t *commands;
    size_t command_count;
    char **directories;
    size_t directory_count;
} dispatcher_state_t;

static dispatcher_state_t state;

static int commands_list(maelys_cli_context_t *context) {
    for (size_t i = 0u; i < state.extensions.count; ++i) {
        const maelys_cli_extension_t *extension = &state.extensions.items[i];
        maelys_cli_json_writer_t writer;
        maelys_cli_json_writer_init(&writer);
        int built = maelys_cli_json_begin_object(&writer) == 0 &&
            maelys_cli_json_key_string(&writer, "command", extension->command) == 0 &&
            maelys_cli_json_key_string(&writer, "executable",
                extension->executable) == 0 &&
            maelys_cli_json_key_string(&writer, "manifest", extension->manifest) == 0 &&
            maelys_cli_json_key_string(&writer, "version", extension->version) == 0 &&
            maelys_cli_json_key_string(&writer, "summary", extension->summary) == 0 &&
            maelys_cli_json_key_unsigned(&writer, "cliApi", extension->cli_api) == 0 &&
            maelys_cli_json_key_boolean(&writer, "digestVerified",
                extension->digest_verified) == 0 &&
            maelys_cli_json_end_object(&writer) == 0;
        char *record = built ? maelys_cli_json_finish(&writer) : NULL;
        if (!record) {
            maelys_cli_json_writer_clear(&writer);
            return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
                "Could not serialize command '%s'.", extension->command);
        }
        char line[512];
        (void)snprintf(line, sizeof(line), "%-16s %-10s %s", extension->command,
            extension->version, extension->summary);
        int emitted = maelys_cli_emit_record(context, record, line);
        free(record);
        if (emitted != 0)
            return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, NULL,
                "Could not write the command list.");
    }
    return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
}

static int split_directories(const char *value) {
    size_t count = 1u;
    for (const char *p = value; *p; ++p) if (*p == ':') ++count;
    state.directories = calloc(count, sizeof(*state.directories));
    if (!state.directories) return -1;
    char *copy = strdup(value);
    if (!copy) return -1;
    char *cursor = copy;
    while (cursor) {
        char *next = strchr(cursor, ':');
        if (next) *next++ = '\0';
        if (*cursor) {
            state.directories[state.directory_count] = strdup(cursor);
            if (!state.directories[state.directory_count]) {
                free(copy);
                return -1;
            }
            state.directory_count++;
        }
        cursor = next;
    }
    free(copy);
    return 0;
}

static int build_catalog(maelys_cli_error_t *error) {
    const char *const *directories;
    size_t directory_count = 0u;
    const char *override = getenv("MAELYS_COMMANDS_PATH");
    if (override && *override) {
        if (split_directories(override) != 0) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, NULL,
                "Out of memory while reading MAELYS_COMMANDS_PATH.");
            return -1;
        }
        directories = (const char *const *)state.directories;
        directory_count = state.directory_count;
    } else {
        directories = maelys_cli_extension_default_directories(&directory_count);
    }
    if (maelys_cli_extension_discover(directories, directory_count,
            &state.extensions, error) != 0)
        return -1;
    maelys_cli_command_t *external = calloc(
        state.extensions.count ? state.extensions.count : 1u, sizeof(*external));
    if (!external) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Out of memory while building the catalog.");
        return -1;
    }
    for (size_t i = 0u; i < state.extensions.count; ++i) {
        const maelys_cli_extension_t *extension = &state.extensions.items[i];
        maelys_cli_command_t *command = &external[i];
        command->id = extension->command;
        command->pattern = extension->command;
        command->purpose = extension->summary[0] ? extension->summary :
            "External command declared by an installed manifest.";
        command->effect = MAELYS_CLI_EFFECT_EXECUTE;
        command->apply_effect = MAELYS_CLI_EFFECT_NONE;
        command->output = MAELYS_CLI_OUTPUT_STREAM;
        command->operands = passthrough_operands;
        command->operand_count = MAELYS_CLI_COUNT(passthrough_operands);
        command->delegate = extension->executable;
    }
    maelys_cli_catalog_part_t parts[] = {
        MAELYS_CLI_CATALOG_PART(builtin_commands),
        {external, state.extensions.count},
    };
    int composed = maelys_cli_catalog_concat(parts, MAELYS_CLI_COUNT(parts),
        &state.commands, &state.command_count);
    free(external);
    if (composed != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Out of memory while building the catalog.");
        return -1;
    }
    return 0;
}

static void release(void) {
    maelys_cli_extension_set_clear(&state.extensions);
    free(state.commands);
    for (size_t i = 0u; i < state.directory_count; ++i) free(state.directories[i]);
    free(state.directories);
    memset(&state, 0, sizeof(state));
}

int main(int argc, char **argv) {
    maelys_cli_error_t error;
    maelys_cli_app_t app = {
        .program = "maelys",
        .product = "Maelys CLI",
        .version = MAELYS_CLI_VERSION,
        .summary = "dispatcher for Maelys command-line tools",
        .agent_guidance =
            "EXTERNAL COMMANDS\n"
            "  External commands come only from manifests installed in the "
            "command directories\n"
            "  (see 'maelys commands list'). Their arguments are passed verbatim; "
            "run 'maelys COMMAND describe'\n"
            "  to inspect their own catalog.",
    };
    if (build_catalog(&error) != 0) {
        release();
        (void)fprintf(stderr, "maelys: [%s] %s\n", error.code, error.message);
        if (error.hint[0]) (void)fprintf(stderr, "Hint: %s\n", error.hint);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    app.commands = state.commands;
    app.command_count = state.command_count;
    int result = maelys_cli_main(&app, argc, argv);
    release();
    return result;
}
