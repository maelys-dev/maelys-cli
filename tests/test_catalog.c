#include "check.h"

#include <maelys/cli.h>

#include <stdlib.h>
#include <string.h>

static int dummy_handler(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", "ok", 0);
}

static const char *const modes[] = {"fast", "safe", NULL};
static const maelys_cli_operand_t operands[] = {
    {MAELYS_CLI_OPERAND("ROOT", "Root directory.")},
    {MAELYS_CLI_OPERAND_OPTIONAL("NAME", "Optional name.")},
    {MAELYS_CLI_OPERAND_REST("PATH", "Remaining paths.")},
};
static const maelys_cli_option_t options[] = {
    {MAELYS_CLI_CHOICE("mode", "Mode.", modes), .default_text = "fast"},
    {MAELYS_CLI_SIZE("size", "BYTES", "Size.", 1u, 0u), .required = 1},
    {MAELYS_CLI_STRING("tag", "TEXT", "Tag."), .repeatable = 1, .depends_on = "size"},
    MAELYS_CLI_APPLY_OPTION,
};

static maelys_cli_command_t good_command(void) {
    maelys_cli_command_t command = {
        MAELYS_CLI_TRANSACTION("thing.make", "thing make", "Make a thing.",
        dummy_handler), MAELYS_CLI_OPERANDS(operands), MAELYS_CLI_OPTIONS(options),
        MAELYS_CLI_SCHEMA("{\"type\":\"object\"}")};
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
    CHECK(strcmp(synopsis, "thing make ROOT [NAME] [PATH...] --size BYTES "
        "[--mode fast|safe] [--tag TEXT...] [--apply]") == 0);
    char *allocated = maelys_cli_command_synopsis_alloc(&command);
    CHECK(allocated && strcmp(allocated, synopsis) == 0);
    free(allocated);
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
        {MAELYS_CLI_CHOICE("mode", "Mode.", NULL)},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = bad_choice;
    command.option_count = 1u;
    CHECK(!validate(&command));

    static const maelys_cli_option_t transport_clash[] = {
        {MAELYS_CLI_STRING("format", NULL, "Clash.")},
    };
    command.options = transport_clash;
    CHECK(!validate(&command));

    static const maelys_cli_option_t dangling[] = {
        {MAELYS_CLI_FLAG("one", "One."), .depends_on = "two"},
    };
    command.options = dangling;
    CHECK(!validate(&command));

    static const maelys_cli_option_t inverted[] = {
        {MAELYS_CLI_UNSIGNED("n", NULL, "N.", 10u, 5u)},
    };
    command.options = inverted;
    CHECK(!validate(&command));

    command = good_command();
    command.protocol = "git-smart"; /* protocol on a non-stream command */
    CHECK(!validate(&command));

    static const maelys_cli_option_t same_lengths[] = {
        {MAELYS_CLI_HEX_OR("oid", "OID", "Oid.", 40u, 40u)},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = same_lengths;
    command.option_count = 1u;
    CHECK(!validate(&command));

    static const maelys_cli_option_t bad_default[] = {
        {MAELYS_CLI_UNSIGNED("n", NULL, "N.", 1u, 9u), .default_text = "10"},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = bad_default;
    command.option_count = 1u;
    CHECK(!validate(&command));

    static const maelys_cli_option_t lonely_group[] = {
        {MAELYS_CLI_FLAG("a", "A."), .group = "g"},
    };
    command.options = lonely_group;
    CHECK(!validate(&command));

    static const char *const unknown_all[] = {"nope", NULL};
    static const maelys_cli_option_t dangling_all[] = {
        {MAELYS_CLI_FLAG("a", "A."), .depends_on_all = unknown_all},
    };
    command.options = dangling_all;
    CHECK(!validate(&command));

    /* Unavailable commands: reason required, no handler nor delegate. */
    command = good_command();
    command.handler = NULL;
    command.unavailable = "not in this build";
    CHECK(validate(&command));
    command.handler = dummy_handler;
    CHECK(!validate(&command));
    command.handler = NULL;
    command.unavailable = "";
    CHECK(!validate(&command));

    /* Oversized synopsis is reported by validation, naming the command. */
    static char long_name[MAELYS_CLI_MAX_SYNOPSIS + 8u];
    memset(long_name, 'X', sizeof(long_name) - 1u);
    long_name[sizeof(long_name) - 1u] = '\0';
    static maelys_cli_operand_t huge[1];
    huge[0] = (maelys_cli_operand_t){MAELYS_CLI_OPERAND(long_name, "Huge.")};
    command = good_command();
    command.operands = huge;
    command.operand_count = 1u;
    maelys_cli_app_t huge_app = {"prog", "Product", "1.0", NULL, &command, 1u, NULL, 0u, NULL, NULL};
    maelys_cli_error_t huge_error;
    CHECK(maelys_cli_catalog_validate(&huge_app, &huge_error) != 0);
    CHECK(strstr(huge_error.message, "thing.make") && strstr(huge_error.message, "synopsis longer"));

    static const char *const unknown_algorithms[] = {"md5", NULL};
    static const maelys_cli_option_t bad_digest[] = {
        {MAELYS_CLI_DIGEST("digest", NULL, "Digest.", unknown_algorithms)},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = bad_digest;
    command.option_count = 1u;
    CHECK(!validate(&command));
    CHECK(maelys_cli_digest_hex_digits("sha512") == 128u);
    CHECK(maelys_cli_digest_hex_digits("md5") == 0u);

    static const maelys_cli_operand_t bad_order[] = {
        {MAELYS_CLI_OPERAND_OPTIONAL("A", "Optional first.")},
        {MAELYS_CLI_OPERAND("B", "Required after optional.")},
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
    CHECK(count == 5u && strcmp(builtins[0].id, "help") == 0);
    CHECK(strcmp(builtins[3].id, "completion") == 0 && strcmp(builtins[4].id, "complete.candidates") == 0 && builtins[4].hidden);
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
