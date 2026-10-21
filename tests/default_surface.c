/* SPDX-License-Identifier: MPL-2.0 */
/* A program that declares a default command (agent-cli/v2, section 8, "A
 * line that names no command"): `show` runs for a line whose first word is
 * an option, `--` or a word with one dash, and for an empty line.
 * python/tests/default_surface.py declares the same catalog, word for word,
 * so that tests/test_cli.sh holds the two implementations to the same
 * answers. Nothing here writes: `save` plans and applies nothing. */
#include "maelys/cli.h"

#include <stdio.h>

static const char *const levels[] = {"low", "high", NULL};

static int command_show(maelys_cli_context_t *context) {
    const char *label = maelys_cli_option(context, "label");
    const char *level = maelys_cli_option(context, "level");
    const char *file = maelys_cli_operand(context, 0u);
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "label", label ? label : "");
    (void)maelys_cli_json_key_string(&data, "level", level ? level : "");
    (void)maelys_cli_json_key_string(&data, "file", file ? file : "");
    (void)maelys_cli_json_end_object(&data);
    char human[1024];
    (void)snprintf(human, sizeof(human), "Showing '%s' at '%s' from '%s'.",
        label ? label : "", level ? level : "", file ? file : "");
    return maelys_cli_succeed_writer(context, &data, human, MAELYS_CLI_EXIT_OK);
}

static int command_list(maelys_cli_context_t *context) {
    const char *limit = maelys_cli_option(context, "limit");
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "limit", limit ? limit : "");
    (void)maelys_cli_json_end_object(&data);
    return maelys_cli_succeed_writer(context, &data, "Listing.", MAELYS_CLI_EXIT_OK);
}

static int command_save(maelys_cli_context_t *context) {
    int apply = maelys_cli_flag(context, "apply");
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "mode", apply ? "apply" : "plan");
    (void)maelys_cli_json_key_boolean(&data, "changed", 0);
    (void)maelys_cli_json_end_object(&data);
    return maelys_cli_succeed_writer(context, &data,
        apply ? "Nothing to save." : "Plan only.", MAELYS_CLI_EXIT_OK);
}

static const maelys_cli_operand_t show_operands[] = {
    {MAELYS_CLI_OPERAND_OPTIONAL("FILE", "File to show.")},
};
static const maelys_cli_option_t show_options[] = {
    {MAELYS_CLI_STRING("label", "TEXT", "Label of what is shown.")},
    {MAELYS_CLI_UNSIGNED("count", "N", "How many lines.", 1u, 10u)},
    {MAELYS_CLI_CHOICE("level", "Level of detail.", levels)},
};
static const maelys_cli_option_t list_options[] = {
    {MAELYS_CLI_UNSIGNED("limit", "N", "Maximum number of entries.", 0u, 1000u)},
};
static const maelys_cli_option_t save_options[] = {
    {MAELYS_CLI_STRING("content", "TEXT", "Text to save."), .required = 1},
    MAELYS_CLI_APPLY_OPTION,
};

static const maelys_cli_command_t commands[] = {
    {MAELYS_CLI_READ("show", "show", "Show a file, the default command.", command_show),
     MAELYS_CLI_OPERANDS(show_operands), MAELYS_CLI_OPTIONS(show_options)},
    {MAELYS_CLI_READ("list", "list", "List the entries.", command_list),
     MAELYS_CLI_OPTIONS(list_options)},
    {MAELYS_CLI_TRANSACTION("save", "save", "Save a text; plans by default.", command_save),
     MAELYS_CLI_OPTIONS(save_options),
     MAELYS_CLI_SCHEMA("{\"type\":\"object\",\"required\":[\"mode\",\"changed\"]}")},
};

static const maelys_cli_app_t default_app = {
    .program = "default-surface",
    .product = "Default surface",
    .version = "1.0.0",
    .summary = "a program whose lines may name no command",
    .commands = commands,
    .command_count = MAELYS_CLI_COUNT(commands),
    .default_command = "show",
};

int main(int argc, char **argv) {
    return maelys_cli_main(&default_app, argc, argv);
}
