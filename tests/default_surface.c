/* SPDX-License-Identifier: MPL-2.0 */
/* A program that declares a default command (agent-cli/v2, section 8, "A
 * line that names no command"): `show` runs for a line whose first word is
 * an option, `--` or a word with one dash, and for an empty line.
 * python/tests/default_surface.py declares the same catalog, word for word,
 * so that tests/test_cli.sh holds the two implementations to the same
 * answers. Nothing here writes: `save` plans and applies nothing.
 *
 * Two variables serve the migration tests. DEFAULT_SURFACE_DEFAULT names
 * the default command (`show` when unset, `none` for no default), so that
 * the same catalog answers before and after a default is declared.
 * DEFAULT_SURFACE_TRACE names a file every handler appends its command to,
 * the one thing a handler writes: a refused line leaves it empty. */
#include "maelys/cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const levels[] = {"low", "high", NULL};

static void trace(const char *command) {
    const char *path = getenv("DEFAULT_SURFACE_TRACE");
    FILE *file = path && *path ? fopen(path, "a") : NULL;
    if (!file) return;
    (void)fprintf(file, "%s\n", command);
    (void)fclose(file);
}

static int command_show(maelys_cli_context_t *context) {
    trace("show");
    const char *label = maelys_cli_option(context, "label");
    const char *level = maelys_cli_option(context, "level");
    const char *file = maelys_cli_operand(context, 0u);
    /* A failure of the product's own, whose template its table translates. */
    if (label && label[0] == '!')
        return maelys_cli_fail(context, MAELYS_CLI_CODE_PRECONDITION_FAILED,
            "Choose a label that does not start with '!'.", "Label %s is reserved.", label);
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
    trace("list");
    const char *limit = maelys_cli_option(context, "limit");
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "limit", limit ? limit : "");
    (void)maelys_cli_json_end_object(&data);
    return maelys_cli_succeed_writer(context, &data, "Listing.", MAELYS_CLI_EXIT_OK);
}

static int command_save(maelys_cli_context_t *context) {
    trace("save");
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

static int command_watch(maelys_cli_context_t *context) {
    (void)context;
    trace("watch");
    return fputs("watching\n", stdout) == EOF ? MAELYS_CLI_EXIT_FAILURE : MAELYS_CLI_EXIT_OK;
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
    {MAELYS_CLI_STREAM("watch", "watch", "Watch the entries on stdout.", command_watch)},
};

/* A palette for every role, so that tests/test_cli.sh holds the colored
 * help of C and Python to the same bytes; and French, so that it holds
 * their help in another language to the same bytes too. */
static const maelys_cli_help_palette_t palette = {
    .heading = "1;34", .command = "36", .option = "33", .placeholder = "4", .example = "32",
};

/* French for part of the catalog: the texts left out stay in English,
 * as a product's do while it is translated. */
static const maelys_cli_translation_t french[] = {
    {"a program whose lines may name no command",
     "un programme dont les lignes peuvent ne nommer aucune commande"},
    {"Show a file, the default command.",
     "Montrer un fichier ; c'est la commande par défaut."},
    {"List the entries.",
     "Lister les entrées."},
    {"Save a text; plans by default.",
     "Enregistrer un texte ; planifie par défaut."},
    {"File to show.",
     "Fichier à montrer."},
    {"Label of what is shown.",
     "Étiquette de ce qui est montré."},
    {"How many lines.",
     "Combien de lignes."},
    {"Maximum number of entries.",
     "Nombre maximal d'entrées."},
    {"Text to save.",
     "Texte à enregistrer."},
    {"Label %s is reserved.",
     "L'étiquette %s est réservée."},
    {"Choose a label that does not start with '!'.",
     "Choisissez une étiquette qui ne commence pas par '!'."},
};
static const maelys_cli_language_t languages[] = {
    {"fr", french, MAELYS_CLI_COUNT(french)},
};

static const maelys_cli_app_t default_app = {
    .program = "default-surface",
    .product = "Default surface",
    .version = "1.0.0",
    .summary = "a program whose lines may name no command",
    .commands = commands,
    .command_count = MAELYS_CLI_COUNT(commands),
    .default_command = "show",
    .help_palette = &palette,
    .languages = languages,
    .language_count = MAELYS_CLI_COUNT(languages),
};

int main(int argc, char **argv) {
    maelys_cli_app_t app = default_app;
    const char *named = getenv("DEFAULT_SURFACE_DEFAULT");
    if (named && *named) app.default_command = strcmp(named, "none") ? named : NULL;
    return maelys_cli_main(&app, argc, argv);
}
