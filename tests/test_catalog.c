#include "check.h"

#include <maelys/cli.h>

#include <errno.h>
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
    {MAELYS_CLI_FLAG("trace", "Trace; diagnostics."), .hidden = 1},
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

    /* A pattern must be a valid extended regular expression. */
    maelys_cli_option_t bad_pattern[] = {
        {MAELYS_CLI_STRING("label", "TEXT", "Label."), .pattern = "^([a-z]+$"},
    };
    command = good_command();
    command.options = bad_pattern;
    command.option_count = 1u;
    CHECK(!validate(&command));

    /* A hidden option is never required. */
    maelys_cli_option_t hidden_required[] = {
        {MAELYS_CLI_FLAG("trace", "Trace."), .hidden = 1, .required = 1},
    };
    command = good_command();
    command.options = hidden_required;
    command.option_count = 1u;
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
    static const maelys_cli_option_t pattern_on_flag[] = {
        {MAELYS_CLI_FLAG("all", "All."), .pattern = "^a$"},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = pattern_on_flag;
    command.option_count = 1u;
    CHECK(!validate(&command));
    static const maelys_cli_option_t operand_conflict[] = {
        {MAELYS_CLI_FLAG("all", "All."), .conflicts_with = "ROOT"},
    };
    static const maelys_cli_option_t unknown_conflict[] = {
        {MAELYS_CLI_FLAG("all", "All."), .conflicts_with = "NOPE"},
    };
    command = good_command();
    command.apply_effect = MAELYS_CLI_EFFECT_NONE;
    command.effect = MAELYS_CLI_EFFECT_READ;
    command.options = operand_conflict;
    command.option_count = 1u;
    CHECK(validate(&command));
    command.options = unknown_conflict;
    CHECK(!validate(&command));
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

    /* --expect on a transaction is the reserved binding of a plan (spec 2.9,
     * section 4): MAELYS_CLI_EXPECT_OPTION whole, and a fingerprint the
     * output schema requires. Every other shape is refused at startup. */
    static const char bound_schema[] =
        "{\"type\":\"object\",\"required\":[\"mode\",\"fingerprint\"]}";
    static const maelys_cli_option_t bound[] = {
        MAELYS_CLI_APPLY_OPTION, MAELYS_CLI_EXPECT_OPTION,
    };
    command = good_command();
    command.options = bound;
    command.option_count = 2u;
    command.output_schema_json = bound_schema;
    CHECK(validate(&command));
    command.output_schema_json = "{\"type\":\"object\",\"required\":[\"mode\"]}";
    CHECK(!validate(&command));                    /* fingerprint not required */
    command.output_schema_json = "{\"type\":\"object\"}";
    CHECK(!validate(&command));                    /* nothing required */
    static const char *const two_algorithms[] = {"sha256", "sha512", NULL};
    maelys_cli_option_t shaped[2] = {MAELYS_CLI_APPLY_OPTION, MAELYS_CLI_EXPECT_OPTION};
    command.options = shaped;
    command.output_schema_json = bound_schema;
    CHECK(validate(&command));
    shaped[1].depends_on = NULL;
    CHECK(!validate(&command));                    /* accepted without --apply */
    shaped[1].depends_on = "apply";
    shaped[1].choices = two_algorithms;
    CHECK(!validate(&command));                    /* another algorithm */
    shaped[1].choices = maelys_cli_expect_algorithms;
    shaped[1].repeatable = 1;
    CHECK(!validate(&command));                    /* repeatable */
    shaped[1].repeatable = 0;
    shaped[1].kind = MAELYS_CLI_VALUE_STRING;
    shaped[1].choices = NULL;
    CHECK(!validate(&command));                    /* another meaning of the name */
    /* The name is reserved on a transaction only. */
    maelys_cli_command_t reading = {
        MAELYS_CLI_READ("look", "look", "A read.", dummy_handler)};
    static const maelys_cli_option_t free_expect[] = {
        {MAELYS_CLI_STRING("expect", "TEXT", "Something else, on a read.")},
    };
    reading.options = free_expect;
    reading.option_count = 1u;
    CHECK(validate(&reading));

    /* An example is an invocation the command accepts (spec 2.12, section
     * 2): the catalog validation parses it and refuses the catalog when it
     * does not parse. Nothing runs it. */
    {
        struct { const char *words; const char *summary; int accepted; } cases[] = {
            {"thing make /root --size 1K", "The least it takes.", 1},
            {"thing make /root --size=1K --mode safe --tag a --tag b", "Both spellings, one repeated.", 1},
            {"thing make /root --size 1K --apply --format json", "A global option.", 1},
            {"thing make --size 1K -- /root", "An operand after --.", 1},
            {"make /root --size 1K", "Not its pattern.", 0},
            {"thing makes /root --size 1K", "A pattern that only begins alike.", 0},
            {"thing make /root", "A required option is missing.", 0},
            {"thing make --size 1K", "A required operand is missing.", 0},
            {"thing make /root name a b --size 1K", "Its optional and its variadic operands.", 1},
            {"thing make /root --size 1K --nosuch", "An option the command has lost.", 0},
            {"thing make /root --size bogus", "A value that is not of its kind.", 0},
            {"thing make /root --size 1K --mode sideways", "A choice it does not offer.", 0},
            {"thing make /root --tag a", "--tag depends on --size.", 0},
            {"thing make /root --size 1K --trace", "A hidden option.", 0},
            {"thing make /root --size 1K --help", "Asks for the help instead.", 0},
            {"thing make  /root --size 1K", "Two spaces.", 0},
            {"thing make /root --size 1K ", "A trailing space.", 0},
            {"thing make /root --size 1K", "", 0},
            {"", "No words.", 0},
            {"thing make /root --size 1K", "A control \033[2J character.", 0},
        };
        for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            maelys_cli_example_t example = {cases[i].words, cases[i].summary};
            command = good_command();
            command.examples = &example;
            command.example_count = 1u;
            if (validate(&command) != cases[i].accepted) {
                (void)fprintf(stderr, "example %zu: '%s' should be %s\n", i,
                    cases[i].words, cases[i].accepted ? "accepted" : "refused");
                CHECK(0);
            }
        }
        /* The refusal names the command, the example and why. */
        maelys_cli_example_t lost = {"thing make /root --size 1K --nosuch", "Lost."};
        command = good_command();
        command.examples = &lost;
        command.example_count = 1u;
        maelys_cli_app_t one = {"prog", "Product", "1.0", NULL, &command, 1u, NULL, 0u, NULL, NULL};
        maelys_cli_error_t refusal;
        CHECK(maelys_cli_catalog_validate(&one, &refusal) != 0);
        CHECK(strstr(refusal.message, "'thing.make'") && strstr(refusal.message, "--nosuch") &&
            strstr(refusal.message, "does not accept"));
        /* After a delegate's pattern the words are the other executable's. */
        maelys_cli_example_t handed = {"tool anything --at --all", "Handed over as it is."};
        maelys_cli_command_t delegate = {
            MAELYS_CLI_EXTERNAL("tool", "tool", "A delegate.", "prog-tool"),
            .examples = &handed, .example_count = 1u};
        CHECK(validate(&delegate));
        maelys_cli_example_t astray = {"other anything", "Not even its pattern."};
        delegate.examples = &astray;
        CHECK(!validate(&delegate));
    }

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

static const maelys_cli_command_t base_part[] = {
    {MAELYS_CLI_READ("alpha", "alpha", "Alpha.", dummy_handler)},
    {MAELYS_CLI_EXECUTE("pro.sign", "pro sign", "Sign.", NULL),
     .unavailable = "provided by the pro build"},
    {MAELYS_CLI_READ("omega", "omega", "Omega.", dummy_handler)},
};
static const maelys_cli_command_t pro_part[] = {
    {MAELYS_CLI_EXECUTE("pro.sign", "pro sign", "Sign for real.", dummy_handler)},
    {MAELYS_CLI_READ("pro.extra", "pro extra", "Extra.", dummy_handler)},
};

static int test_concat(void) {
    maelys_cli_command_t *commands = NULL;
    size_t count = 0u;
    maelys_cli_catalog_part_t parts[] = {
        MAELYS_CLI_CATALOG_PART(base_part),
        {NULL, 0u},
        MAELYS_CLI_CATALOG_PART(pro_part),
    };
    CHECK(maelys_cli_catalog_concat(parts, 3u, &commands, &count) == 0);
    CHECK(count == 4u);
    /* The override keeps the position of the base declaration. */
    CHECK(strcmp(commands[1].id, "pro.sign") == 0 && commands[1].handler == dummy_handler);
    CHECK(commands[1].unavailable == NULL && strcmp(commands[1].purpose, "Sign for real.") == 0);
    CHECK(strcmp(commands[2].id, "omega") == 0 && strcmp(commands[3].id, "pro.extra") == 0);
    maelys_cli_app_t app = {"prog", "Product", "1.0", NULL, commands, count, NULL, 0u, NULL, NULL};
    maelys_cli_error_t error;
    CHECK(maelys_cli_catalog_validate(&app, &error) == 0);
    free(commands);
    /* The base alone keeps the unavailable declaration and still validates. */
    maelys_cli_catalog_part_t base_only[] = {MAELYS_CLI_CATALOG_PART(base_part)};
    CHECK(maelys_cli_catalog_concat(base_only, 1u, &commands, &count) == 0 && count == 3u);
    CHECK(commands[1].unavailable != NULL && commands[1].handler == NULL);
    free(commands);
    /* Empty composition and refused arguments. */
    CHECK(maelys_cli_catalog_concat(NULL, 0u, &commands, &count) == 0 && count == 0u && commands != NULL);
    free(commands);
    /* A later part may not shadow a command the earlier part provides. */
    static const maelys_cli_command_t shadowing[] = {
        {MAELYS_CLI_READ("alpha", "alpha", "Alpha again.", dummy_handler)},
    };
    maelys_cli_catalog_part_t shadow_parts[] = {
        MAELYS_CLI_CATALOG_PART(base_part), MAELYS_CLI_CATALOG_PART(shadowing),
    };
    commands = (maelys_cli_command_t *)1;
    CHECK(maelys_cli_catalog_concat(shadow_parts, 2u, &commands, &count) != 0);
    CHECK(errno == EEXIST && commands == NULL);
    maelys_cli_catalog_part_t broken[] = {{NULL, 2u}};
    CHECK(maelys_cli_catalog_concat(broken, 1u, &commands, &count) != 0 && errno == EINVAL);
    CHECK(maelys_cli_catalog_concat(parts, 3u, NULL, &count) != 0 && errno == EINVAL);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_synopsis);
    RUN(test_validation);
    RUN(test_names);
    RUN(test_concat);
    return failures ? 1 : 0;
}
