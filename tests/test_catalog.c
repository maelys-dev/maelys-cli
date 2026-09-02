#include "check.h"

#include <maelys/cli.h>

#include <string.h>

static int dummy_handler(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", "ok", 0);
}

static const char *const modes[] = {"fast", "safe", NULL};
static const maelys_cli_operand_t operands[] = {
    {"ROOT", "Root directory.", 1, 0},
    {"NAME", "Optional name.", 0, 0},
    {"PATH", "Remaining paths.", 0, 1},
};
static const maelys_cli_option_t options[] = {
    {"mode", MAELYS_CLI_VALUE_CHOICE, NULL, "Mode.", 0, 0, NULL, NULL, modes,
     0u, 0u, 0, 0, 0u, "fast"},
    {"size", MAELYS_CLI_VALUE_SIZE, "BYTES", "Size.", 1, 0, NULL, NULL, NULL,
     1u, 0u, 0, 0, 0u, NULL},
    {"tag", MAELYS_CLI_VALUE_STRING, "TEXT", "Tag.", 0, 1, "size", NULL, NULL,
     0u, 0u, 0, 0, 0u, NULL},
    MAELYS_CLI_APPLY_OPTION,
};

static maelys_cli_command_t good_command(void) {
    maelys_cli_command_t command = {
        "thing.make", "thing make", "Make a thing.", MAELYS_CLI_EFFECT_PREVIEW,
        MAELYS_CLI_EFFECT_APPLY, MAELYS_CLI_OUTPUT_ENVELOPE,
        MAELYS_CLI_OPERANDS(operands), MAELYS_CLI_OPTIONS(options),
        "{\"type\":\"object\"}", dummy_handler, NULL, NULL, 0};
    return command;
}

static int validate(const maelys_cli_command_t *command) {
    maelys_cli_app_t app = {"prog", "Product", "1.0", NULL, command, 1u, NULL, 0u, NULL, NULL};
    maelys_cli_error_t error;
    return maelys_cli_catalog_validate(&app, &error) == 0;
}

static int test_synopsis(void) {
    maelys_cli_command_t command = good_command();
    char synopsis[256];
    CHECK(maelys_cli_command_synopsis(&command, synopsis, sizeof(synopsis)) == 0);
    CHECK(strcmp(synopsis, "thing make ROOT [NAME] [PATH...] [--mode fast|safe] "
        "--size BYTES [--tag TEXT...] [--apply]") == 0);
    CHECK(maelys_cli_command_synopsis(&command, synopsis, 10u) != 0);
    command.synopsis = "custom";
    CHECK(maelys_cli_command_synopsis(&command, synopsis, sizeof(synopsis)) == 0);
    CHECK(strcmp(synopsis, "custom") == 0);
    return 1;
}

static int test_validation(void) {
    maelys_cli_command_t command = good_command();
    CHECK(validate(&command));

    command = good_command();
    command.id = "Thing";
    CHECK(!validate(&command));

    command = good_command();
    command.pattern = "thing --make";
    CHECK(!validate(&command));

    command = good_command();
    command.purpose = "";
    CHECK(!validate(&command));

    command = good_command();
    command.effect = MAELYS_CLI_EFFECT_READ; /* apply_effect without preview */
    CHECK(!validate(&command));

    command = good_command();
    command.option_count = 3u; /* transaction without --apply */
    CHECK(!validate(&command));

    command = good_command();
    command.output_schema_json = "{\"type\":";
    CHECK(!validate(&command));

    command = good_command();
    command.output_schema_json = "[]";
    CHECK(!validate(&command));

    command = good_command();
    command.handler = NULL;
    CHECK(!validate(&command));
    command.delegate = "helper";
    CHECK(!validate(&command)); /* delegates cannot declare options */
    command.option_count = 0u;
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_EXECUTE;
    CHECK(validate(&command));

    static const maelys_cli_option_t bad_choice[] = {
        {"mode", MAELYS_CLI_VALUE_CHOICE, NULL, "Mode.", 0, 0, NULL, NULL, NULL,
         0u, 0u, 0, 0, 0u, NULL},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = bad_choice;
    command.option_count = 1u;
    CHECK(!validate(&command));

    static const maelys_cli_option_t transport_clash[] = {
        {"format", MAELYS_CLI_VALUE_STRING, NULL, "Clash.", 0, 0, NULL, NULL,
         NULL, 0u, 0u, 0, 0, 0u, NULL},
    };
    command.options = transport_clash;
    CHECK(!validate(&command));

    static const maelys_cli_option_t dangling[] = {
        {"one", MAELYS_CLI_VALUE_NONE, NULL, "One.", 0, 0, "two", NULL, NULL,
         0u, 0u, 0, 0, 0u, NULL},
    };
    command.options = dangling;
    CHECK(!validate(&command));

    static const maelys_cli_option_t inverted[] = {
        {"n", MAELYS_CLI_VALUE_UNSIGNED, NULL, "N.", 0, 0, NULL, NULL, NULL,
         10u, 5u, 0, 0, 0u, NULL},
    };
    command.options = inverted;
    CHECK(!validate(&command));

    static const maelys_cli_operand_t bad_order[] = {
        {"A", "Optional first.", 0, 0},
        {"B", "Required after optional.", 1, 0},
    };
    command = good_command();
    command.operands = bad_order;
    command.operand_count = 2u;
    CHECK(!validate(&command));

    /* Duplicate identifiers across the catalog and clash with a builtin. */
    maelys_cli_command_t pair[2] = {good_command(), good_command()};
    maelys_cli_app_t app = {"prog", "Product", "1.0", NULL, pair, 2u, NULL, 0u, NULL, NULL};
    maelys_cli_error_t error;
    CHECK(maelys_cli_catalog_validate(&app, &error) != 0);
    CHECK(strstr(error.message, "collides") != NULL);
    pair[1].id = "help";
    pair[1].pattern = "help";
    app.command_count = 2u;
    CHECK(maelys_cli_catalog_validate(&app, &error) != 0);
    return 1;
}

static int test_names(void) {
    CHECK(strcmp(maelys_cli_value_kind_name(MAELYS_CLI_VALUE_SIZE), "size") == 0);
    CHECK(strcmp(maelys_cli_effect_name(MAELYS_CLI_EFFECT_PREVIEW), "preview") == 0);
    CHECK(strcmp(maelys_cli_output_mode_name(MAELYS_CLI_OUTPUT_STREAM), "protocol-stream") == 0);
    size_t count = 0u;
    const maelys_cli_command_t *builtins = maelys_cli_builtin_commands(&count);
    CHECK(count == 3u && strcmp(builtins[0].id, "help") == 0);
    CHECK(strcmp(builtins[1].id, "version") == 0 && strcmp(builtins[2].id, "describe") == 0);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_synopsis);
    RUN(test_validation);
    RUN(test_names);
    return failures ? 1 : 0;
}
