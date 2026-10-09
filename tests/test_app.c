#include "check.h"

#include <maelys/cli.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ---- fixture catalog ------------------------------------------------------- */

static const char *const levels[] = {"low", "high", NULL};
static const char *const digest_algorithms[] = {"sha256", "sha1", NULL};
static size_t last_digest_algorithm;
static int helper_lookup_result;
static int helper_lookup_errno;
static const maelys_cli_operand_t make_operands[] = {
    {MAELYS_CLI_OPERAND("ROOT", "Root.")},
    {MAELYS_CLI_OPERAND("NAME", "Name.")},
};
static const maelys_cli_option_t make_options[] = {
    {MAELYS_CLI_FLAG("legacy", "Legacy behavior; diagnostics."), .hidden = 1},
    {MAELYS_CLI_PATH("git", "FILE", "Git path.")},
    {MAELYS_CLI_CHOICE("level", "Level.", levels), .default_text = "low"},
    {MAELYS_CLI_SIZE("memory", "BYTES", "Memory.", 1u, 0u)},
    {MAELYS_CLI_DURATION("wait", "DURATION", "Wait.", 0u, 0u)},
    {MAELYS_CLI_INTEGER("offset", "N", "Offset.", -10, 10)},
    {MAELYS_CLI_STRING("tag", "TEXT", "Tag."), .repeatable = 1},
    {MAELYS_CLI_HEX_OR("oid", "OID", "Object id.", 4u, 8u)},
    {MAELYS_CLI_ABSOLUTE_PATH("store", "DIR", "Store directory.")},
    {MAELYS_CLI_DIGEST("digest", NULL, "Pinned digest.", digest_algorithms)},
    {MAELYS_CLI_FLAG("strict", "Strict."), .depends_on = "git"},
    {MAELYS_CLI_FLAG("lenient", "Lenient."), .conflicts_with = "strict"},
    MAELYS_CLI_APPLY_OPTION,
    {MAELYS_CLI_STRING("label", "TEXT", "Lowercase label."), .pattern = "^[a-z]+$"},
};

static int last_memory_present;
static uint64_t last_memory;
static uint64_t last_wait;
static int64_t last_offset;
static size_t last_level;

static int make_runs;

static int command_make(maelys_cli_context_t *context) {
    ++make_runs;
    last_memory_present = maelys_cli_option_unsigned(context, "memory", &last_memory);
    (void)maelys_cli_option_unsigned(context, "wait", &last_wait);
    (void)maelys_cli_option_integer(context, "offset", &last_offset);
    (void)maelys_cli_option_choice(context, "level", &last_level);
    last_digest_algorithm = 99u;
    (void)maelys_cli_option_choice(context, "digest", &last_digest_algorithm);
    char helper[1024];
    helper_lookup_result = maelys_cli_resolve_helper(context,
        "maelys-test-absent-helper", helper, sizeof(helper));
    helper_lookup_errno = errno;
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "mode",
        maelys_cli_flag(context, "apply") ? "apply" : "plan");
    (void)maelys_cli_json_key_string(&data, "root", maelys_cli_operand(context, 0u));
    (void)maelys_cli_json_key_string(&data, "git", maelys_cli_option_or(context, "git", "/usr/bin/git"));
    (void)maelys_cli_json_key_unsigned(&data, "tags", (uint64_t)maelys_cli_option_count(context, "tag"));
    (void)maelys_cli_json_end_object(&data);
    return maelys_cli_succeed_writer(context, &data, "made", MAELYS_CLI_EXIT_OK);
}

static const char *const shells[] = {"bash", "zsh", NULL};
static const maelys_cli_operand_t typed_operands[] = {
    {MAELYS_CLI_OPERAND_CHOICE("SHELL", "Shell.", shells)},
    {MAELYS_CLI_OPERAND_OPTIONAL("COUNT", "Count."),
     .kind = MAELYS_CLI_VALUE_UNSIGNED, .minimum = 1u, .maximum = 9u},
};
static size_t last_shell;
static uint64_t last_count;
static int last_count_present;

static int command_typed(maelys_cli_context_t *context) {
    last_shell = 99u;
    (void)maelys_cli_operand_choice(context, 0u, &last_shell);
    last_count_present = maelys_cli_operand_unsigned(context, 1u, &last_count);
    return maelys_cli_succeed(context, "{}", "typed", MAELYS_CLI_EXIT_OK);
}

static const char *const mirror_all[] = {"source-oid", "target-oid", NULL};
#define TEST_MAX_RETRIES 7
static const maelys_cli_option_t mirror_options[] = {
    {MAELYS_CLI_UNSIGNED("attempts", "N", "Attempts.", 0u, 10u),
     MAELYS_CLI_DEFAULT_OF(TEST_MAX_RETRIES)},
    {MAELYS_CLI_HEX("source-oid", "OID", "Expected source.", 4u),
     .group = "preconditions"},
    {MAELYS_CLI_HEX("target-oid", "OID", "Expected target.", 4u),
     .group = "preconditions"},
    {MAELYS_CLI_UNSIGNED("retries", "N", "Retries.", 0u, 5u), .default_text = "2"},
    {MAELYS_CLI_CHOICE("mode", "Mode.", levels), .default_text = "high"},
    {MAELYS_CLI_FLAG("apply", "Apply."), .depends_on_all = mirror_all},
};
static uint64_t mirror_retries;
static size_t mirror_mode;
static int mirror_retries_delivered;

/* The rules a command states itself (spec 2.5). policy is maelys-warden's
 * case: five sources of policy, exactly one of them. */
static const maelys_cli_option_t policy_options[] = {
    {MAELYS_CLI_FLAG("ro", "Read-only policy.")},
    {MAELYS_CLI_FLAG("rw", "Read-write policy.")},
    {MAELYS_CLI_PATH("plan", "FILE", "Policy from a plan file.")},
    {MAELYS_CLI_FLAG("trace", "Trace the decision.")},
    {MAELYS_CLI_FLAG("verbose-trace", "Trace every step.")},
    {MAELYS_CLI_FLAG("quiet", "Say nothing.")},
};
static const char *const policy_sources[] = {"ro", "rw", "plan", NULL};
/* at-most-one must not name what the requires chain below demands: a
 * fixture whose rules contradict each other tests nothing. */
static const char *const policy_traces[] = {"trace", "quiet", NULL};
static const char *const policy_chain[] = {"verbose-trace", "trace", "plan", NULL};
static const maelys_cli_constraint_t policy_constraints[] = {
    {MAELYS_CLI_CONSTRAINT(MAELYS_CLI_CONSTRAINT_EXACTLY_ONE, policy_sources)},
    {MAELYS_CLI_CONSTRAINT(MAELYS_CLI_CONSTRAINT_AT_MOST_ONE, policy_traces)},
    {MAELYS_CLI_CONSTRAINT(MAELYS_CLI_CONSTRAINT_REQUIRES, policy_chain)},
};

static int command_policy(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", "policy", MAELYS_CLI_EXIT_OK);
}

/* An operand describes its value exactly as an argument does (spec 2.6):
 * a pattern, enforced by the parser and stated by describe. */
static const maelys_cli_operand_t named_operands[] = {
    {MAELYS_CLI_OPERAND("LABEL", "A matched label."),
     .kind = MAELYS_CLI_VALUE_STRING, .pattern = "^[a-z][a-z0-9-]*$"},
    {MAELYS_CLI_OPERAND_OPTIONAL("SUM", "A fixed-width hex sum."),
     .kind = MAELYS_CLI_VALUE_HEX, .hex_digits = 8},
};

static int command_named(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", "named", MAELYS_CLI_EXIT_OK);
}

static int mirror_runs;

static int command_mirror(maelys_cli_context_t *context) {
    ++mirror_runs;
    mirror_retries = 99u;
    mirror_retries_delivered = maelys_cli_option_unsigned(context, "retries", &mirror_retries);
    (void)maelys_cli_option_choice(context, "mode", &mirror_mode);
    return maelys_cli_succeed(context, "{}", maelys_cli_option_or(context, "mode", "?"),
        MAELYS_CLI_EXIT_OK);
}

/* Two records, then the source breaks: nothing may have reached stdout. */
static int command_broken(maelys_cli_context_t *context) {
    if (maelys_cli_emit_record(context, "{\"i\":0}", "zero") != 0 ||
        maelys_cli_emit_record_trusted(context, "{\"i\":1}", "one") != 0)
        return 1;
    return maelys_cli_fail(context, MAELYS_CLI_CODE_IO_FAILED, "Retry.",
        "The source broke after two records.");
}

/* A transaction that binds its application to the reviewed plan: the plan
 * is "write bound_state + 1", its fingerprint covers the state it reads. */
static int bound_state;
static int bound_writes;

static int command_bound(maelys_cli_context_t *context) {
    maelys_cli_fingerprint_t plan;
    char fingerprint[MAELYS_CLI_FINGERPRINT_SIZE];
    char state[32];
    (void)snprintf(state, sizeof(state), "%d", bound_state);
    maelys_cli_fingerprint_init(&plan);
    maelys_cli_fingerprint_add_string(&plan, "state", state);
    maelys_cli_fingerprint_finish(&plan, fingerprint);
    int refused = maelys_cli_expect(context, fingerprint);
    if (refused) return refused;
    int apply = maelys_cli_flag(context, "apply");
    if (apply) {
        ++bound_writes;
        ++bound_state;
    }
    maelys_cli_json_writer_t data;
    maelys_cli_json_writer_init(&data);
    (void)maelys_cli_json_begin_object(&data);
    (void)maelys_cli_json_key_string(&data, "mode", apply ? "apply" : "plan");
    (void)maelys_cli_json_key_string(&data, "fingerprint", fingerprint);
    (void)maelys_cli_json_end_object(&data);
    return maelys_cli_succeed_writer(context, &data, fingerprint, MAELYS_CLI_EXIT_OK);
}

/* The same declaration over a handler that never asks maelys_cli_expect():
 * the framework does not let it answer as if the binding had been checked. */
static int command_careless(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context,
        "{\"mode\":\"apply\",\"fingerprint\":\"sha256:0\"}", "done", MAELYS_CLI_EXIT_OK);
}

static const maelys_cli_option_t bound_options[] = {
    MAELYS_CLI_APPLY_OPTION, MAELYS_CLI_EXPECT_OPTION,
};
static const char bound_schema[] =
    "{\"type\":\"object\",\"required\":[\"mode\",\"fingerprint\"]}";

static int command_trusted(maelys_cli_context_t *context) {
    if (context->invocation->command->output == MAELYS_CLI_OUTPUT_RECORDS) {
        (void)maelys_cli_emit_record_trusted(context, "{\"i\":0}", "zero");
        (void)maelys_cli_emit_record_trusted(context, "{\"i\":1}", "one");
        return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
    }
    return maelys_cli_succeed_trusted(context, "{\"trusted\":true}", "trusted",
        MAELYS_CLI_EXIT_OK);
}
static const maelys_cli_option_t trusted_options[] = {
    {MAELYS_CLI_FLAG("records", "Emit records instead.")},
};

static const maelys_cli_operand_t rest_operands[] = {
    {MAELYS_CLI_OPERAND("COMMAND", "Command.")},
    {MAELYS_CLI_OPERAND_REST("ARG", "Args.")},
};

static int command_exec(maelys_cli_context_t *context) {
    /* Stream command: writes nothing through the framework. */
    (void)fprintf(context->out, "%zu\n", maelys_cli_operand_count(context));
    return 7;
}

static int command_records(maelys_cli_context_t *context) {
    for (int i = 0; i < 2; ++i) {
        char record[64];
        (void)snprintf(record, sizeof(record), "{\"i\":%d}", i);
        if (maelys_cli_emit_record(context, record, i ? "one" : "zero") != 0) return 1;
    }
    return maelys_cli_finish_records(context, MAELYS_CLI_EXIT_OK);
}

static int command_report(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{\"valid\":false}", "invalid", MAELYS_CLI_EXIT_VIOLATIONS);
}

static int command_fail(maelys_cli_context_t *context) {
    return maelys_cli_fail(context, MAELYS_CLI_CODE_NOT_FOUND, "Create it.", "Missing %s.", "thing");
}

static int helper_may_fail(maelys_cli_context_t *context, int fail) {
    if (fail) return maelys_cli_fail(context, MAELYS_CLI_CODE_NOT_FOUND,
        "Create it.", "Helper failed.");
    return MAELYS_CLI_EXIT_OK;
}

static int command_delegating(maelys_cli_context_t *context) {
    int fail = maelys_cli_flag(context, "fail");
    int result = helper_may_fail(context, fail);
    if (maelys_cli_replied(context)) return result;
    return maelys_cli_succeed(context, "{\"helper\":true}", "helper ok",
        MAELYS_CLI_EXIT_OK);
}

static const maelys_cli_option_t delegating_options[] = {
    {MAELYS_CLI_FLAG("fail", "Make the helper fail.")},
};

static int command_silent(maelys_cli_context_t *context) {
    (void)context;
    return 0;
}

static int command_bad_json(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{broken", NULL, 0);
}

static const maelys_cli_operand_t long_example_operands[] = {
    {MAELYS_CLI_OPERAND_REST("WORD", "Words.")},
};
static const maelys_cli_option_t long_example_options[] = {
    {MAELYS_CLI_STRING("first-quite-long-option", "TEXT", "First.")},
    {MAELYS_CLI_STRING("second-quite-long-option", "TEXT", "Second.")},
};
static const maelys_cli_example_t long_examples[] = {
    {MAELYS_CLI_EXAMPLE("long-example --first-quite-long-option one "
     "--second-quite-long-option two -- /usr/local/bin/agent --once",
     "Longer than eighty columns, and whole.")},
    {MAELYS_CLI_EXAMPLE("long-example --first-quite-long-option=$HOME "
     "a=b:c,d/e.f-g_h@i%j+k $HOME *.c it's a\\b =first a;b caf\xc3\xa9",
     "Words a shell would read otherwise.")},
};

static const maelys_cli_command_t commands[] = {
    {MAELYS_CLI_TRANSACTION("thing.make", "thing make", "Make.", command_make),
     MAELYS_CLI_OPERANDS(make_operands), MAELYS_CLI_OPTIONS(make_options),
     MAELYS_CLI_SCHEMA("{\"type\":\"object\",\"required\":[\"mode\"]}")},
    {MAELYS_CLI_PROTOCOL_STREAM("exec", "exec", "Exec.", command_exec, "test-jsonl"),
     MAELYS_CLI_OPERANDS(rest_operands)},
    {MAELYS_CLI_RECORDS("records", "records", "Records.", command_records)},
    {MAELYS_CLI_READ("long-example", "long-example", "An example past the width.", command_report),
     MAELYS_CLI_OPERANDS(long_example_operands), MAELYS_CLI_OPTIONS(long_example_options),
     MAELYS_CLI_EXAMPLES(long_examples), .hidden = 1},
    {MAELYS_CLI_TRANSACTION("bound", "bound", "A plan bound to its application.", command_bound),
     MAELYS_CLI_OPTIONS(bound_options), MAELYS_CLI_SCHEMA(bound_schema), .hidden = 1},
    {MAELYS_CLI_TRANSACTION("careless", "careless", "Declares --expect, never checks it.",
     command_careless),
     MAELYS_CLI_OPTIONS(bound_options), MAELYS_CLI_SCHEMA(bound_schema), .hidden = 1},
    {MAELYS_CLI_RECORDS("broken", "broken", "Records, then a failure.", command_broken),
     .hidden = 1},
    {MAELYS_CLI_READ("report", "report", "Report.", command_report)},
    {MAELYS_CLI_READ("typed", "typed", "Typed operands.", command_typed),
     MAELYS_CLI_OPERANDS(typed_operands)},
    {MAELYS_CLI_TRANSACTION("mirror", "mirror", "Mirror.", command_mirror),
     MAELYS_CLI_OPTIONS(mirror_options)},
    {MAELYS_CLI_READ("policy", "policy", "Policy.", command_policy),
     MAELYS_CLI_OPTIONS(policy_options), MAELYS_CLI_CONSTRAINTS(policy_constraints)},
    {MAELYS_CLI_READ("named", "named", "Named.", command_named),
     MAELYS_CLI_OPERANDS(named_operands)},
    {MAELYS_CLI_READ("trusted", "trusted", "Trusted output.", command_trusted),
     MAELYS_CLI_OPTIONS(trusted_options), .hidden = 1},
    {MAELYS_CLI_RECORDS("trusted-records", "trusted-records", "Trusted records.",
     command_trusted), .hidden = 1},
    {.id = "cloud", .pattern = "cloud", .purpose = "Cloud sync.",
     .effect = MAELYS_CLI_EFFECT_EXECUTE, .output = MAELYS_CLI_OUTPUT_ENVELOPE,
     .unavailable = "built without the Cloud agent"},
    /* An unavailable command may name the code that fits its cause: a
     * refusal of trust is not an absence, and an agent reads the code. */
    {MAELYS_CLI_READ("sealed", "sealed", "Sealed component.", NULL),
     .unavailable = "the component does not match its declared digest",
     .unavailable_code = MAELYS_CLI_CODE_ACCESS_DENIED},
    {MAELYS_CLI_READ("delegating", "delegating", "Delegating.", command_delegating),
     MAELYS_CLI_OPTIONS(delegating_options), .hidden = 1},
    {MAELYS_CLI_READ("fail", "fail", "Fail.", command_fail), .hidden = 1},
    {MAELYS_CLI_READ("silent", "silent", "Silent.", command_silent), .hidden = 1},
    {MAELYS_CLI_READ("badjson", "badjson", "Bad JSON.", command_bad_json),
     .hidden = 1},
    {MAELYS_CLI_EXTERNAL("missing", "missing", "Missing helper.",
     "maelys-test-missing-helper"), .hidden = 1},
};

static const maelys_cli_app_t app = {
    "prog", "Test Product", "9.9.9", "fixture", commands,
    MAELYS_CLI_COUNT(commands), NULL, 0u,
    /* A heading, a paragraph written on one line as the header asks, a blank
     * line, and a deeper paragraph: each line is wrapped at its indentation. */
    "EXTRA GUIDANCE\n"
    "  A product writes each paragraph of its guidance on one line, however long "
    "that line is, and the help wraps it to its width at the indentation it was "
    "given.\n"
    "\n"
    "    Deeper: a second paragraph, indented by four, is wrapped at four and "
    "never brought back to the margin of the first one.",
    NULL
};

/* ---- harness ---------------------------------------------------------------- */

typedef struct run_result {
    int code;
    char *out;
    char *err;
} run_result_t;

static run_result_t run(int argc, char **argv) {
    run_result_t result = {0, NULL, NULL};
    size_t out_size = 0u;
    size_t err_size = 0u;
    FILE *out = open_memstream(&result.out, &out_size);
    FILE *err = open_memstream(&result.err, &err_size);
    result.code = maelys_cli_run(&app, argc, argv, out, err);
    (void)fclose(out);
    (void)fclose(err);
    return result;
}

static void release(run_result_t *result) {
    free(result->out);
    free(result->err);
}

static int expect_failure(run_result_t *result, const char *code, const char *fragment);

#define ARGV(...) (char *[]){__VA_ARGS__}
#define COUNT(...) ((int)(sizeof((char *[]){__VA_ARGS__}) / sizeof(char *)))
#define RUNV(...) run(COUNT(__VA_ARGS__), ARGV(__VA_ARGS__))

/* The widest line of a text, in bytes: columns for the ASCII of these tests. */
static size_t widest_line(const char *text) {
    size_t widest = 0u;
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > widest) widest = length;
        line = end ? end + 1 : line + length;
    }
    return widest;
}

/* A second program, for what the help of the first cannot show: a purpose
 * whose letters take more bytes than columns. */
static int command_nothing(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", "", MAELYS_CLI_EXIT_OK);
}
static const maelys_cli_command_t accented_commands[] = {
    /* Twelve words of six columns and ten bytes, then three CJK characters:
     * wrapped by columns, nine words fit a line of 80 after the label;
     * wrapped by bytes, six would. */
    {MAELYS_CLI_READ("wide", "wide",
     "\xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 \xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 "
     "\xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 \xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 "
     "\xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 \xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 "
     "\xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 \xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 "
     "\xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 \xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 "
     "\xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 \xc3\xa9t\xc3\xa9\xc3\xa9t\xc3\xa9 "
     "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.", command_nothing)},
};
static const maelys_cli_app_t accented_app = {
    "prog", "Test Product", "9.9.9", NULL, accented_commands, 1u, NULL, 0u, NULL, NULL
};

static int test_help_and_version(void) {
    run_result_t result = run(0, NULL);
    CHECK(result.code == 0 && strstr(result.out, "COMMANDS") && !result.err[0]);
    /* The general help names a command by its pattern and its purpose; its
     * usage is in its own help and in its family's. */
    CHECK(strstr(result.out, "\n  thing make") && !strstr(result.out, "thing make ROOT NAME"));
    CHECK(strstr(result.out, "prog help COMMAND_ID") && strstr(result.out, "prog help FAMILY"));
    CHECK(strstr(result.out, "EXTRA GUIDANCE"));
    /* The product's guidance follows the width like the rest: each of its
     * lines is a paragraph wrapped at that line's indentation. 0.6.0 printed
     * it as written, so a paragraph on one line was one line of any length
     * -- 533 columns for one product -- and `maelys help` itself passed 80. */
    CHECK(strstr(result.out, "\nEXTRA GUIDANCE\n  A product writes each paragraph of its guidance on one line, however long that\n"
        "  line is, and the help wraps it to its width at the indentation it was given.\n"));
    CHECK(strstr(result.out, "given.\n\n    Deeper: a second paragraph, indented by four, is wrapped at four and never\n"
        "    brought back to the margin of the first one.\n"));
    CHECK(!strstr(result.out, "badjson")); /* hidden */
    /* The product's commands come first, then the ones every program has. */
    const char *own = strstr(result.out, "\n  thing make");
    const char *built_in = strstr(result.out, "\n  describe");
    CHECK(own && built_in && own < built_in);
    /* It fits eighty columns where it is not written to a terminal: no
     * description is pushed to the right of the longest usage. */
    CHECK(widest_line(result.out) <= 80u);
    /* What every program has in common is named, not repeated: the options
     * by their names, the contract by its first rule, the rest one command
     * away. Spelled out, they were two thirds of the screen. */
    CHECK(strstr(result.out, "prog help conventions") && strstr(result.out, "--format, --json,"));
    CHECK(strstr(result.out, "AGENT CONTRACT") && strstr(result.out, "describe --summary"));
    CHECK(!strstr(result.out, "Exact alias of --format json") && !strstr(result.out, "Exit 0 is success"));
    release(&result);
    result = RUNV("help", "conventions");
    CHECK(result.code == 0 && !result.err[0] && widest_line(result.out) <= 80u &&
        strstr(result.out, "prog - conventions") &&
        strstr(result.out, "Exact alias of --format json") && strstr(result.out, "Exit 0 is success"));
    release(&result);
    result = RUNV("help", "conventions", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"commands\":[]"));   /* a topic holds no command */
    release(&result);
    /* An example is a line to copy: on one line, even past the width. 0.6.1
     * wrapped it without a mark, and each half ran as a command of its own. */
    result = RUNV("help", "long-example");
    CHECK(result.code == 0 && strstr(result.out,
        "\nEXAMPLES\n  prog long-example --first-quite-long-option one --second-quite-long-option two"
        " -- /usr/local/bin/agent --once\n      Longer than eighty columns, and whole.\n"));
    /* And a line a shell reads as declared: a word no shell interprets goes
     * bare, any other single-quoted, a quote and a backslash outside the
     * quotes. Printed raw, `$HOME` was the reader's home and `*.c` their
     * files. `describe` keeps the words themselves. */
    CHECK(strstr(result.out,
        "\n  prog long-example '--first-quite-long-option=$HOME' a=b:c,d/e.f-g_h@i%j+k "
        "'$HOME' '*.c' 'it'\\''s' 'a'\\\\'b' '=first' 'a;b' 'caf\xc3\xa9'\n"
        "      Words a shell would read otherwise.\n"));
    release(&result);
    result = RUNV("describe", "long-example", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"$HOME\",\"*.c\",\"it's\",\"a\\\\b\",\"=first\""));
    release(&result);
    result = RUNV("help", "thing.make");
    CHECK(result.code == 0 && widest_line(result.out) <= 80u &&
        strstr(result.out, "USAGE\n  prog thing make ROOT NAME"));
    /* A usage breaks between its groups, never inside one. */
    CHECK(!strstr(result.out, "[--git\n") && !strstr(result.out, "[--level\n"));
    release(&result);
    /* A family: the commands under an identifier, with their usage and the
     * purpose below; `help FAMILY` and `FAMILY --help` say the same. */
    result = RUNV("help", "thing");
    CHECK(result.code == 0 && !result.err[0] && widest_line(result.out) <= 80u &&
        strstr(result.out, "prog thing - commands") &&
        strstr(result.out, "\n  thing make ROOT NAME") && strstr(result.out, "\n      Make."));
    char *by_identifier = result.out;
    result.out = NULL;
    release(&result);
    result = RUNV("thing", "--help");
    CHECK(result.code == 0 && strcmp(result.out, by_identifier) == 0);
    free(by_identifier);
    release(&result);
    result = RUNV("help", "thing", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"command\":\"help\"") &&
        strstr(result.out, "\"commands\":[\"thing.make\"]"));
    release(&result);
    /* Without --help, words that name no command are still an error. */
    result = RUNV("thing");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "[INVALID_COMMAND]"));
    release(&result);
    result = RUNV("nope", "--help");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "[INVALID_COMMAND]"));
    release(&result);
    /* `FAMILY --help` is an invocation of `help`, and the rest of the line
     * is parsed as one. Reading only the family and --help answered the
     * whole help to each of these, and nothing to --format jsonl. */
    result = RUNV("thing", "--help", "--field", "commands");
    CHECK(result.code == 0 && strcmp(result.out, "thing.make\n") == 0);
    release(&result);
    result = RUNV("thing", "--help", "--bogus");
    CHECK(result.code == 1 && !result.out[0] &&
        strstr(result.err, "Option --bogus is not supported by 'help'"));
    release(&result);
    result = RUNV("thing", "--help", "--format", "xml");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "[VALIDATION_FAILED]"));
    release(&result);
    result = RUNV("thing", "--help", "extra");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "[INVALID_COMMAND]"));
    release(&result);
    result = RUNV("thing", "--help", "--format", "jsonl", "--compact");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "\"command\":\"help\"") &&
        strstr(result.err, "'help' does not produce records and cannot render jsonl."));
    release(&result);
    /* Wrapped by columns, not by bytes: an accented letter is one column
     * and a CJK one two. */
    {
        run_result_t wide = {0, NULL, NULL};
        size_t out_size = 0u;
        size_t err_size = 0u;
        FILE *out = open_memstream(&wide.out, &out_size);
        FILE *err = open_memstream(&wide.err, &err_size);
        wide.code = maelys_cli_run(&accented_app, 1, ARGV("help"), out, err);
        (void)fclose(out);
        (void)fclose(err);
        const char *entry = strstr(wide.out, "\n  wide ");
        CHECK(wide.code == 0 && entry);
        const char *end = strchr(entry + 1, '\n');
        size_t bytes = (size_t)(end - entry - 1);
        /* The label and its column take 14; nine words of six columns and
         * their eight spaces take 62: 76 columns, in 112 bytes, each word
         * being ten bytes. Wrapped by bytes, six words would fit. */
        CHECK(bytes == 112u);
        /* The last line carries three CJK characters, six columns. */
        CHECK(strstr(wide.out, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.\n"));
        release(&wide);
    }
    result = RUNV("--help");
    CHECK(result.code == 0 && strstr(result.out, "USAGE"));
    release(&result);
    result = RUNV("thing", "make", "--help");
    CHECK(result.code == 0 && strstr(result.out, "--memory BYTES") && strstr(result.out, "preview by default"));
    CHECK(!strstr(result.out, "--legacy")); /* hidden option: absent from usage and help */
    release(&result);
    result = RUNV("help", "exec");
    CHECK(result.code == 0 && strstr(result.out, "protocol-stream owned by protocol test-jsonl"));
    release(&result);
    /* `COMMAND --help` answers as `help COMMAND_ID` does, envelope included:
     * `command` names help, whose data this is, and data.commands the
     * command asked about. 0.5.36 and earlier named the command itself over
     * data its output schema does not describe. */
    result = RUNV("thing", "make", "--help", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"command\":\"help\",\"ok\":true") &&
        strstr(result.out, "\"commands\":[\"thing.make\"]"));
    char *asked_by_option = result.out;
    result.out = NULL;
    release(&result);
    result = RUNV("help", "thing.make", "--json", "--compact");
    CHECK(result.code == 0 && strcmp(result.out, asked_by_option) == 0);
    free(asked_by_option);
    release(&result);
    result = RUNV("version", "--help", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"command\":\"help\"") &&
        strstr(result.out, "\"commands\":[\"version\"]") && !strstr(result.out, "\"product\""));
    release(&result);
    /* `X --help` is validated as an invocation of `help`: jsonl has no form
     * for a help, and the line is refused as `help X --format jsonl` is. It
     * rendered nothing and exited 0, the costliest answer in a pipe. A
     * records command asked for its help is no exception: the help is not
     * records. With --field, one member renders. */
    {
        run_result_t direct = RUNV("help", "thing.make", "--format", "jsonl");
        CHECK(direct.code == 1 && !direct.out[0] &&
            strstr(direct.err, "'help' does not produce records and cannot render jsonl."));
        result = RUNV("thing", "make", "--help", "--format", "jsonl");
        CHECK(result.code == 1 && !result.out[0] && strcmp(result.err, direct.err) == 0);
        release(&result);
        result = RUNV("records", "--help", "--format", "jsonl");
        CHECK(result.code == 1 && !result.out[0] && strcmp(result.err, direct.err) == 0);
        release(&result);
        release(&direct);
    }
    result = RUNV("thing", "make", "--help", "--format", "jsonl", "--field", "commands");
    CHECK(result.code == 0 && strcmp(result.out, "\"thing.make\"\n") == 0);
    release(&result);
    /* Nothing runs under --help, whatever else the line carries. */
    mirror_runs = 0;
    result = RUNV("mirror", "--apply", "--source-oid", "abcd", "--target-oid", "ef01", "--help");
    CHECK(result.code == 0 && mirror_runs == 0 && strstr(result.out, "USAGE"));
    release(&result);
    result = RUNV("help", "nope");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "[INVALID_COMMAND]"));
    release(&result);
    result = RUNV("--version");
    CHECK(result.code == 0 && strcmp(result.out, "prog 9.9.9\n") == 0);
    release(&result);
    result = RUNV("version", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"product\":\"Test Product\""));
    CHECK(strstr(result.out, "\"contract\":\"agent-cli/v2\"") && !result.err[0]);
    release(&result);
    return 1;
}

static int test_describe(void) {
    run_result_t result = RUNV("describe", "--format", "json", "--compact");
    CHECK(result.code == 0 && !result.err[0]);
    CHECK(strstr(result.out, "{\"schemaVersion\":2,\"contract\":\"agent-cli/v2\",\"command\":\"describe\",\"ok\":true,\"exitCode\":0,\"data\":{"));
    CHECK(strstr(result.out, "\"kind\":\"catalog\"") && strstr(result.out, "\"outputSchema\""));
    CHECK(strstr(result.out, "\"effect\":{\"plan\":\"preview\",\"apply\":\"apply\"}"));
    CHECK(strstr(result.out, "\"usage\":\"thing make ROOT NAME [--git FILE] [--level low|high]"));
    CHECK(strstr(result.out, "\"kind\":\"requires\",\"options\":[\"--strict\",\"--git\"]"));
    CHECK(strstr(result.out, "\"kind\":\"at-most-one\",\"options\":[\"--lenient\",\"--strict\"]"));
    CHECK(strstr(result.out, "\"invariants\""));
    CHECK(maelys_cli_json_validate(result.out, strlen(result.out) - 1u, NULL) == 0);
    release(&result);
    result = RUNV("describe", "--summary", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"kind\":\"summary\"") && !strstr(result.out, "outputSchema"));
    CHECK(!strstr(result.out, "\"filter\""));
    release(&result);
    /* Filtered discovery: the namespace of a prefix, hidden included. */
    result = RUNV("describe", "--summary", "--prefix", "thing", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"filter\":{\"kind\":\"command-prefix\",\"value\":\"thing\"}"));
    CHECK(strstr(result.out, "\"id\":\"thing.make\"") && !strstr(result.out, "\"id\":\"exec\""));
    release(&result);
    result = RUNV("describe", "--summary", "--prefix", "thing.make", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"id\":\"thing.make\""));
    release(&result);
    result = RUNV("describe", "--summary", "--prefix", "thin", "--json", "--compact");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "\"code\":\"INVALID_COMMAND\""));
    release(&result);
    result = RUNV("describe", "--summary", "--prefix", "Thing.");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Option --prefix expects a value matching"));
    result = RUNV("describe", "--prefix", "thing");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--prefix requires --summary"));
    result = RUNV("describe", "thing.make", "--summary", "--prefix", "thing");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--prefix conflicts with operand COMMAND_ID"));
    result = RUNV("describe", "describe", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"long\":\"--prefix\"") && strstr(result.out, "\"conflictsWith\":[\"COMMAND_ID\"]"));
    /* An operand conflict stays in conflictsWith; input.constraints names options only (spec 2.3 kit). */
    CHECK(strstr(result.out, "\"conflictsWith\":[\"COMMAND_ID\"]"));
    CHECK(!strstr(result.out, "\"options\":[\"--prefix\",\"COMMAND_ID\"]"));
    CHECK(strstr(result.out, "\"pattern\":\"^[a-z]([a-z0-9.-]*[a-z0-9-])?$\""));
    release(&result);
    result = RUNV("describe", "thing.make", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"kind\":\"command\""));
    CHECK(strstr(result.out, "\"outputSchema\":{\"type\":\"object\",\"required\":[\"mode\"]}"));
    /* Hidden option: listed with "hidden": true, absent from the synopsis; the
     * member is emitted only when true. */
    CHECK(strstr(result.out, "\"long\":\"--legacy\",\"required\":false,\"repeatable\":false,\"summary\":\"Legacy behavior; diagnostics.\",\"requires\":[],\"conflictsWith\":[],\"hidden\":true}"));
    CHECK(!strstr(result.out, "\"long\":\"--git\",\"required\":false,\"repeatable\":false,\"summary\":\"Git path.\",\"argument\":{\"name\":\"FILE\",\"type\":\"path\"},\"requires\":[],\"conflictsWith\":[],\"hidden\""));
    CHECK(!strstr(result.out, "[--legacy]"));
    CHECK(strstr(result.out, "\"choices\":[\"low\",\"high\"]") && strstr(result.out, "\"default\":\"low\""));
    CHECK(strstr(result.out, "\"minimum\":-10,\"maximum\":10"));
    /* An unsigned kind states a bound only when declared: a maximum of 0 is
     * unbounded and absent, not UINT64_MAX; a minimum of 0 is the floor. */
    CHECK(strstr(result.out, "\"argument\":{\"name\":\"BYTES\",\"type\":\"size\",\"minimum\":1}"));
    CHECK(strstr(result.out, "\"argument\":{\"name\":\"DURATION\",\"type\":\"duration\"}"));
    CHECK(!strstr(result.out, "18446744073709551615"));
    /* Two accepted lengths are the array the contract declares, not a second
     * member: this assertion used to pin `alternativeDigits`, which the
     * `argument` definition never allowed. */
    CHECK(strstr(result.out, "\"digits\":[4,8]"));
    CHECK(!strstr(result.out, "alternativeDigits"));
    CHECK(strstr(result.out, "\"type\":\"absolute-path\""));
    CHECK(strstr(result.out, "\"type\":\"digest\",\"algorithms\":[\"sha256\",\"sha1\"]"));
    CHECK(strstr(result.out, "[--digest ALGORITHM:HEX]"));
    release(&result);
    result = RUNV("describe", "exec", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"outputMode\":\"protocol-stream\",\"protocol\":\"test-jsonl\""));
    release(&result);
    result = RUNV("describe", "nope", "--json", "--compact");
    CHECK(result.code == 1 && !result.out[0]);
    CHECK(strstr(result.err, "\"ok\":false") && strstr(result.err, "\"code\":\"INVALID_COMMAND\""));
    release(&result);
    result = RUNV("describe");
    CHECK(result.code == 0 && result.out[0] == '{' && strstr(result.out, "\n  \"kind\": \"catalog\""));
    release(&result);
    return 1;
}

static int test_parsing_success(void) {
    run_result_t result = RUNV("thing", "make", "/root", "name", "--memory", "2K",
        "--wait=1500ms", "--offset", "-3", "--level", "high", "--tag", "a",
        "--tag=b", "--git", "--literal", "--format", "json", "--compact");
    CHECK(result.code == 0 && !result.err[0]);
    CHECK(strcmp(result.out, "{\"schemaVersion\":2,\"contract\":\"agent-cli/v2\","
        "\"command\":\"thing.make\",\"ok\":true,\"exitCode\":0,\"data\":{\"mode\":\"plan\","
        "\"root\":\"/root\",\"git\":\"--literal\",\"tags\":2}}\n") == 0);
    CHECK(last_memory_present && last_memory == 2048u && last_wait == 1500u);
    CHECK(last_offset == -3 && last_level == 1u);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--legacy", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"ok\":true")); /* hidden option accepted */
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--oid", "0123abcd", "--json", "--compact");
    CHECK(result.code == 0 && !result.err[0]);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--store", "/var/store",
        "--digest", "sha1:0123456789abcdef0123456789abcdef01234567");
    CHECK(result.code == 0 && last_digest_algorithm == 1u);
    CHECK(helper_lookup_result == -1 && helper_lookup_errno == ENOENT);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--apply", "--strict", "--git", "/g");
    CHECK(result.code == 0 && strcmp(result.out, "made\n") == 0 && !last_memory_present);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--apply=false", "--json=false");
    CHECK(result.code == 0 && strcmp(result.out, "made\n") == 0);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--pretty=false", "--json");
    CHECK(result.code == 0 && result.out[0] == '{' && !strchr(result.out, ' '));
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--json");
    CHECK(result.code == 0 && strstr(result.out, "\n  \"data\": {\n    \"mode\": \"plan\""));
    release(&result);
    return 1;
}

static int expect_failure(run_result_t *result, const char *code, const char *fragment) {
    int ok = result->code == 1 && !result->out[0] && strstr(result->err, code) != NULL &&
        (!fragment || strstr(result->err, fragment) != NULL);
    if (!ok) (void)fprintf(stderr, "unexpected: code=%d out=[%s] err=[%s]\n",
        result->code, result->out, result->err);
    release(result);
    return ok;
}

static int test_parsing_failures(void) {
    run_result_t result = RUNV("things");
    CHECK(expect_failure(&result, "[INVALID_COMMAND]", "Unknown prog command: things"));
    result = RUNV("bad\033[2J\nline\233\330\234\342\200\216\342\200\217\342\200\256");
    CHECK(result.code == 1 && strstr(result.err,
        "bad\\x1b[2J\\nline\\x9b\\u061c\\u200e\\u200f\\u202e"));
    CHECK(strchr(result.err, '\033') == NULL && strchr(result.err, '\233') == NULL);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--token", "x");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--token is not supported"));
    result = RUNV("thing", "make", "/root", "name", "--dry-run");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Add --apply only"));
    result = RUNV("thing", "make", "/root", "name", "--git", "/a", "--git", "/b");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "only once"));
    result = RUNV("thing", "make", "/root", "name", "--git");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "requires a value"));
    result = RUNV("thing", "make", "/root", "name", "--memory", "0");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "byte size between 1"));
    result = RUNV("thing", "make", "/root", "name", "--wait", "10");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "duration with a unit"));
    result = RUNV("thing", "make", "/root", "name", "--offset", "11");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "between -10 and 10"));
    result = RUNV("thing", "make", "/root", "name", "--oid", "abcde");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "expects 4 or 8 lowercase"));
    result = RUNV("thing", "make", "/root", "name", "--store", "relative/dir");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "expects an absolute path"));
    result = RUNV("thing", "make", "/root", "name", "--digest", "md5:abcd");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "sha256:64, sha1:40"));
    result = RUNV("thing", "make", "/root", "name", "--digest", "sha256:abcd");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "ALGORITHM:HEX"));
    result = RUNV("thing", "make", "/root", "name", "--level", "medium");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "one of: low, high"));
    result = RUNV("thing", "make", "/root", "name", "--apply=maybe");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "expects a boolean"));
    result = RUNV("thing", "make", "/root", "name", "--strict");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--strict requires --git"));
    result = RUNV("thing", "make", "/root", "name", "--strict", "--git", "/g", "--lenient");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--lenient conflicts with --strict"));
    result = RUNV("thing", "make", "/root");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operands do not match"));
    result = RUNV("thing", "make", "/root", "name", "extra");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operands do not match"));
    result = RUNV("thing", "make", "/root", "name", "--format", "xml");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "one of: text, json, jsonl"));
    result = RUNV("thing", "make", "/root", "name", "--format", "jsonl", "--compact");
    /* The requested jsonl rendering also selects the JSON error envelope. */
    CHECK(expect_failure(&result, "\"code\":\"VALIDATION_FAILED\"", "cannot render jsonl"));
    result = RUNV("exec", "cmd", "--json", "--compact");
    CHECK(expect_failure(&result, "\"code\":\"VALIDATION_FAILED\"", "stream command"));
    result = RUNV("thing", "make", "/root", "name", "--git=");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "non-empty value"));
    /* JSON error envelope on stderr even when parsing fails. */
    result = RUNV("thing", "make", "--format=json", "--compact");
    CHECK(result.code == 1 && !result.out[0]);
    CHECK(strstr(result.err, "{\"schemaVersion\":2,\"contract\":\"agent-cli/v2\",\"command\":\"thing.make\",\"ok\":false,\"exitCode\":1,\"error\":{\"code\":\"VALIDATION_FAILED\""));
    CHECK(strstr(result.err, "\"hint\":\"Use the synopsis"));
    release(&result);
    return 1;
}

static int test_stream_records_and_codes(void) {
    run_result_t result = RUNV("exec", "cmd", "--", "--not-an-option", "x");
    CHECK(result.code == 7 && strcmp(result.out, "3\n") == 0 && !result.err[0]);
    release(&result);
    result = RUNV("records", "--format", "jsonl");
    CHECK(result.code == 0 && strcmp(result.out, "{\"i\":0}\n{\"i\":1}\n") == 0 && !result.err[0]);
    release(&result);
    result = RUNV("records", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"data\":{\"count\":2,\"records\":[{\"i\":0},{\"i\":1}]}"));
    release(&result);
    /* Text records into a pipe: the tabular form of spec 2.3 section 7, the
     * human line being reserved for a terminal. */
    result = RUNV("records");
    CHECK(result.code == 0 && strcmp(result.out, "0\n1\n") == 0 && !result.err[0]);
    release(&result);
    /* Trunk diagnostics: silent in JSON, stdout unchanged in text, never a
     * failure rendering; duplicates and invalid choices refused. */
    result = RUNV("version", "--verbose", "--progress", "always", "--json", "--compact");
    CHECK(result.code == 0 && !result.err[0] && strstr(result.out, "\"ok\":true"));
    release(&result);
    result = RUNV("version", "--verbose", "--progress", "always", "--pager", "always");
    CHECK(result.code == 0 && strcmp(result.out, "prog 9.9.9\n") == 0 && !strstr(result.err, "prog: ["));
    release(&result);
    result = RUNV("version", "--verbose=false", "--progress=never", "--pager=never");
    CHECK(result.code == 0 && strcmp(result.out, "prog 9.9.9\n") == 0 && !result.err[0]);
    release(&result);
    result = RUNV("version", "--verbose", "--verbose");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "may be supplied only once"));
    result = RUNV("version", "--pager=sometimes");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Option --pager expects one of"));
    result = RUNV("exec", "hello", "--pager", "never");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "does not support CLI output rendering"));
    /* --progress and --verbose are not rendering options: a stream accepts them. */
    result = RUNV("exec", "--verbose", "--progress", "never", "hello");
    CHECK(result.code == 7 && strcmp(result.out, "1\n") == 0); /* the stream ran */
    release(&result);
    /* A declared pattern is enforced, in the ERE dialect. */
    result = RUNV("thing", "make", "/root", "name", "--label", "OK");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Option --label expects a value matching ^[a-z]+$"));
    result = RUNV("thing", "make", "/root", "name", "--label", "ok", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"ok\":true"));
    release(&result);
    /* The summary carries none of the catalog-wide members. */
    result = RUNV("describe", "--summary", "--json", "--compact");
    CHECK(result.code == 0 && !strstr(result.out, "globalOptions") && !strstr(result.out, "\"invariants\""));
    release(&result);
    result = RUNV("report", "--json", "--compact");
    CHECK(result.code == 2 && strstr(result.out, "\"ok\":true,\"exitCode\":2,\"data\":{\"valid\":false}"));
    CHECK(!result.err[0]);
    release(&result);
    result = RUNV("fail", "--json", "--compact");
    CHECK(result.code == 1 && !result.out[0]);
    CHECK(strstr(result.err, "\"error\":{\"code\":\"NOT_FOUND\",\"message\":\"Missing thing.\",\"hint\":\"Create it.\"}"));
    release(&result);
    result = RUNV("fail");
    CHECK(result.code == 1 && strcmp(result.err, "prog: [NOT_FOUND] Missing thing.\nHint: Create it.\n") == 0);
    release(&result);
    result = RUNV("delegating");
    CHECK(result.code == 0 && strcmp(result.out, "helper ok\n") == 0);
    release(&result);
    result = RUNV("delegating", "--fail");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "Helper failed."));
    release(&result);
    result = RUNV("silent");
    CHECK(result.code == 1 && strstr(result.err, "[UNEXPECTED]") && strstr(result.err, "without reporting"));
    release(&result);
    result = RUNV("badjson");
    CHECK(result.code == 1 && !result.out[0] && strstr(result.err, "invalid JSON data"));
    release(&result);
    result = RUNV("missing", "arg");
    CHECK(result.code == 1 && strstr(result.err, "[NOT_FOUND]") && strstr(result.err, "not installed"));
    release(&result);
    return 1;
}

/* --field NAME, spec 2.4: renders one top-level member of data instead of
 * the whole result, by the section 7 pipe rules extended to every shape. */
static int test_field(void) {
    /* Scalar. */
    run_result_t result = RUNV("describe", "--field", "program");
    CHECK(result.code == 0 && strcmp(result.out, "prog\n") == 0 && !result.err[0]);
    release(&result);
    result = RUNV("describe", "--field", "program", "--format", "jsonl");
    CHECK(result.code == 0 && strcmp(result.out, "\"prog\"\n") == 0);
    release(&result);
    /* Array of scalars. */
    result = RUNV("describe", "--field", "invariants");
    CHECK(result.code == 0 && strstr(result.out, "usage and agent discovery share one catalog\n"));
    release(&result);
    /* Array of objects: the same tab-separated rows as ordinary records. */
    result = RUNV("describe", "--field", "globalOptions");
    CHECK(result.code == 0 && strstr(result.out, "--format") && strstr(result.out, "\t"));
    release(&result);
    result = RUNV("describe", "--field", "globalOptions", "--format", "jsonl");
    CHECK(result.code == 0 && strstr(result.out, "\"long\":\"--format\""));
    release(&result);
    /* Object: one row, its members as columns. */
    result = RUNV("describe", "--field", "output");
    CHECK(result.code == 0 && strstr(result.out, "agent-cli/v2\t"));
    release(&result);
    /* Refused with --format json (data would not validate against its
     * declared outputSchema once filtered), the alias included; both
     * requested JSON rendering, so the refusal itself is JSON too. */
    result = RUNV("describe", "--field", "program", "--json", "--compact");
    CHECK(result.code == 1 && !result.out[0] &&
        strstr(result.err, "\"code\":\"VALIDATION_FAILED\"") &&
        strstr(result.err, "conflicts with --format json"));
    release(&result);
    result = RUNV("describe", "--field", "program", "--format", "json", "--compact");
    CHECK(result.code == 1 && !result.out[0] &&
        strstr(result.err, "\"code\":\"VALIDATION_FAILED\"") &&
        strstr(result.err, "conflicts with --format json"));
    release(&result);
    /* A name absent from data: found only once the command has run. A
     * jsonl failure still renders as a JSON envelope, as any failure does. */
    result = RUNV("describe", "--field", "no-such-member");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "names 'no-such-member'"));
    result = RUNV("describe", "--field", "no-such-member", "--format", "jsonl", "--compact");
    CHECK(result.code == 1 && !result.out[0] &&
        strstr(result.err, "\"code\":\"VALIDATION_FAILED\"") &&
        strstr(result.err, "names 'no-such-member'"));
    release(&result);
    /* A rendering option: refused by a stream, jsonl allowed with it on a
     * command that is not json-records, duplicates refused like any option. */
    result = RUNV("exec", "hello", "--field", "program");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "does not support CLI output rendering"));
    result = RUNV("describe", "--field", "program", "--field", "program");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "may be supplied only once"));
    /* On a json-records command: records equals the ordinary rendering,
     * count is its own scalar, and jsonl streams records the usual way. */
    result = RUNV("records", "--field", "records");
    CHECK(result.code == 0 && strcmp(result.out, "0\n1\n") == 0);
    release(&result);
    result = RUNV("records", "--field", "count");
    CHECK(result.code == 0 && strcmp(result.out, "2\n") == 0);
    release(&result);
    result = RUNV("records", "--field", "records", "--format", "jsonl");
    CHECK(result.code == 0 && strcmp(result.out, "{\"i\":0}\n{\"i\":1}\n") == 0);
    release(&result);
    return 1;
}

static int test_typed_operands_and_completion(void) {
    run_result_t result = RUNV("typed", "zsh", "7");
    CHECK(result.code == 0 && last_shell == 1u && last_count_present && last_count == 7u);
    release(&result);
    result = RUNV("typed", "bash");
    CHECK(result.code == 0 && last_shell == 0u && !last_count_present);
    release(&result);
    result = RUNV("typed", "fish");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operand SHELL expects one of: bash, zsh"));
    result = RUNV("typed", "bash", "10");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operand COUNT expects an unsigned integer between 1 and 9"));
    result = RUNV("describe", "typed", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"name\":\"SHELL\",\"required\":true,\"variadic\":false,\"summary\":\"Shell.\",\"type\":\"choice\",\"choices\":[\"bash\",\"zsh\"]"));
    CHECK(strstr(result.out, "\"name\":\"COUNT\"") && strstr(result.out, "\"type\":\"unsigned\",\"minimum\":1,\"maximum\":9"));
    CHECK(!strstr(result.out, "globalOptions") && !strstr(result.out, "invariants"));
    release(&result);
    result = RUNV("describe", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "globalOptions") && strstr(result.out, "invariants"));
    release(&result);

    result = RUNV("completion", "bash");
    CHECK(result.code == 0 && strstr(result.out, "complete -o filenames -F _prog_complete prog"));
    CHECK(strstr(result.out, "\"prog\" __complete --"));
    /* bash 3.2 joins the slice into one word once IFS is a newline: the
     * words are taken first. */
    const char *slice = strstr(result.out, "words=(\"${COMP_WORDS[@]:1:COMP_CWORD}\")");
    const char *separator = strstr(result.out, "local IFS=$'\\n'");
    CHECK(slice && separator && slice < separator);
    release(&result);
    result = RUNV("completion", "zsh");
    /* zsh reads ${words[@]:1:CURRENT-1} as the modifier `C`; an empty
     * answer is an empty array; the file loads sourced or from fpath. */
    CHECK(result.code == 0 && strstr(result.out, "\"${(@)words[2,CURRENT]}\""));
    CHECK(!strstr(result.out, ":1:CURRENT") && strstr(result.out, "candidates=(${(f)\"$("));
    CHECK(strstr(result.out, "if [[ ${funcstack[1]} == _prog ]]; then"));
    release(&result);
    result = RUNV("completion", "fish");
    CHECK(result.code == 0 && strstr(result.out, "$words[2..-1] \"$current\""));
    CHECK(strstr(result.out, "__fish_complete_path \"$current\""));
    release(&result);
    result = RUNV("completion", "fish", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"shell\":\"fish\",\"script\":\"# fish completion"));
    release(&result);
    result = RUNV("completion", "powershell");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operand SHELL expects one of: bash, zsh, fish"));

    result = RUNV("__complete", "--", "t");
    CHECK(result.code == 0 && strcmp(result.out, "thing\ntyped\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "thing", "");
    CHECK(result.code == 0 && strcmp(result.out, "make\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "thing", "make", "/r", "n", "--le");
    CHECK(result.code == 0 && strcmp(result.out, "--level\n--lenient\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "thing", "make", "--level", "");
    CHECK(result.code == 0 && strcmp(result.out, "low\nhigh\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "thing", "make", "--level=h");
    CHECK(result.code == 0 && strcmp(result.out, "--level=high\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "thing", "make", "--digest", "");
    CHECK(result.code == 0 && strcmp(result.out, "sha256:\nsha1:\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "help", "th");
    CHECK(result.code == 0 && strcmp(result.out, "thing.make\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "describe", "");
    /* Neither a hidden nor an unavailable command is offered, as a pattern
     * word or as an identifier. */
    CHECK(result.code == 0 && strstr(result.out, "complete.candidates") == NULL &&
        strstr(result.out, "cloud\n") == NULL && strstr(result.out, "sealed\n") == NULL &&
        strstr(result.out, "thing.make\n") != NULL);
    release(&result);
    /* An operand declared without a kind is free text: no word, so the
     * shell offers files. Its kind reads as the flag's and is no boolean. */
    result = RUNV("__complete", "--", "thing", "make", "");
    CHECK(result.code == 0 && result.out[0] == '\0');
    release(&result);
    /* After a delegate's pattern the words are the delegate's: none when it
     * is not installed, in every format, and never a failure. */
    result = RUNV("__complete", "--", "missing", "");
    CHECK(result.code == 0 && result.out[0] == '\0' && result.err[0] == '\0');
    release(&result);
    result = RUNV("__complete", "--format", "json", "--compact", "--", "missing", "--");
    CHECK(result.code == 0 && result.err[0] == '\0' &&
        strstr(result.out, "\"ok\":true") && strstr(result.out, "\"count\":0,\"records\":[]"));
    release(&result);
    result = RUNV("__complete", "--", "clo");
    CHECK(result.code == 0 && result.out[0] == '\0'); /* unavailable: not offered */
    release(&result);
    result = RUNV("__complete", "--", "typed", "z");
    CHECK(result.code == 0 && strcmp(result.out, "zsh\n") == 0);
    release(&result);
    result = RUNV("__complete", "--", "thing", "make", "--git", "");
    CHECK(result.code == 0 && result.out[0] == '\0'); /* files: shell fallback */
    release(&result);
    result = RUNV("__complete", "--", "exec", "x", "--");
    CHECK(result.code == 0 && !strstr(result.out, "--format")); /* stream: no rendering flags */
    release(&result);
    /* Rendering options come before "--"; everything after is a word. */
    result = RUNV("__complete", "--format", "json", "--compact", "--", "thing", "make", "--");
    CHECK(result.code == 0 && strstr(result.out, "\"records\":[{\"word\":\"--git\"}"));
    release(&result);
    return 1;
}

static int test_groups_defaults_unavailable(void) {
    run_result_t result = RUNV("mirror");
    CHECK(result.code == 0 && strcmp(result.out, "high\n") == 0);
    CHECK(mirror_retries_delivered && mirror_retries == 2u && mirror_mode == 1u);
    release(&result);
    result = RUNV("mirror", "--retries", "4", "--mode", "low");
    CHECK(result.code == 0 && mirror_retries == 4u && mirror_mode == 0u);
    release(&result);
    /* A default declared from a library constant is delivered and described. */
    result = RUNV("describe", "mirror", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"long\":\"--attempts\"") &&
        strstr(result.out, "\"default\":\"7\""));
    release(&result);
    result = RUNV("mirror", "--source-oid", "abcd");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "belongs to group 'preconditions' and requires --target-oid"));
    result = RUNV("mirror", "--apply");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--apply requires --source-oid"));
    result = RUNV("mirror", "--apply", "--source-oid", "abcd", "--target-oid", "ef01");
    CHECK(result.code == 0);
    release(&result);
    result = RUNV("describe", "mirror", "--json", "--compact");
    CHECK(result.code == 0);
    /* No name on the entry (spec 2.5): its options are the whole rule, and
     * the name stays on each option's `group`. */
    CHECK(strstr(result.out, "\"kind\":\"all-or-none\",\"options\":[\"--source-oid\",\"--target-oid\"]"));
    CHECK(!strstr(result.out, "\"kind\":\"all-or-none\",\"group\""));
    CHECK(strstr(result.out, "\"long\":\"--apply\"") && strstr(result.out, "\"requires\":[\"--source-oid\",\"--target-oid\"]"));
    CHECK(strstr(result.out, "\"group\":\"preconditions\"}"));
    CHECK(strstr(result.out, "\"usage\":\"mirror [--attempts N] [--source-oid OID] [--target-oid OID] [--retries N] [--mode low|high] [--apply]\""));
    release(&result);
    /* Required options come first in the derived synopsis. */
    result = RUNV("describe", "thing.make", "--json", "--compact");
    CHECK(strstr(result.out, "\"usage\":\"thing make ROOT NAME [--git FILE]"));
    release(&result);
    result = RUNV("cloud");
    CHECK(result.code == 1 && strstr(result.err, "[UNSUPPORTED]") && strstr(result.err, "built without the Cloud agent"));
    release(&result);
    result = RUNV("describe", "cloud", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"available\":false,\"unavailableReason\":\"built without the Cloud agent\""));
    release(&result);
    result = RUNV("help");
    CHECK(strstr(result.out, "Cloud sync. (unavailable in this build)"));
    release(&result);
    /* The declared code answers instead of UNSUPPORTED, which would have
     * said absence where the cause is a refusal of trust. */
    result = RUNV("sealed");
    CHECK(result.code == 1 && strstr(result.err, "[ACCESS_DENIED]") &&
        strstr(result.err, "does not match its declared digest"));
    release(&result);
    result = RUNV("describe", "sealed", "--json", "--compact");
    CHECK(result.code == 0 &&
        strstr(result.out, "\"available\":false,\"unavailableReason\":\"the component"));
    release(&result);
    result = RUNV("trusted", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, ",\"data\":{\"trusted\":true}}\n"));
    release(&result);
    result = RUNV("trusted", "--json");
    CHECK(result.code == 0 && strstr(result.out, "\n  \"data\": {\n    \"trusted\": true"));
    release(&result);
    result = RUNV("trusted-records", "--format", "jsonl");
    CHECK(result.code == 0 && strcmp(result.out, "{\"i\":0}\n{\"i\":1}\n") == 0);
    release(&result);
    result = RUNV("trusted-records", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"data\":{\"count\":2,\"records\":[{\"i\":0},{\"i\":1}]}"));
    release(&result);
    /* A failure leaves stdout empty in every format (section 7): the jsonl
     * lines are held until the command succeeds. 0.5.35 and earlier wrote
     * each as it came, and a command that failed after two had answered
     * two lines and an error. */
    char *formats[] = {"jsonl", "json", "text"};
    for (size_t i = 0u; i < 3u; ++i) {
        result = RUNV("broken", "--format", formats[i]);
        CHECK(result.code == 1 && result.out[0] == '\0' &&
            strstr(result.err, "IO_FAILED") && strstr(result.err, "broke after two records"));
        release(&result);
    }
    result = RUNV("records", "--format", "jsonl");
    CHECK(result.code == 0 && strcmp(result.out, "{\"i\":0}\n{\"i\":1}\n") == 0);
    release(&result);
    return 1;
}

/* --expect binds --apply to the reviewed plan (spec 2.9, section 4). */
static int test_expect(void) {
    bound_state = 7;
    bound_writes = 0;
    run_result_t result = RUNV("bound", "--field", "fingerprint");
    CHECK(result.code == 0 && strncmp(result.out, "sha256:", 7u) == 0 &&
        strlen(result.out) == MAELYS_CLI_FINGERPRINT_SIZE);       /* 71 characters and a newline */
    char reviewed[MAELYS_CLI_FINGERPRINT_SIZE];
    memcpy(reviewed, result.out, MAELYS_CLI_FINGERPRINT_SIZE - 1u);
    reviewed[MAELYS_CLI_FINGERPRINT_SIZE - 1u] = '\0';
    release(&result);
    /* Two plans over the same state carry the same fingerprint. */
    result = RUNV("bound", "--field", "fingerprint");
    CHECK(result.code == 0 && strncmp(result.out, reviewed, strlen(reviewed)) == 0);
    release(&result);
    /* --expect belongs to --apply, and is a sha256 digest. */
    result = RUNV("bound", "--expect", reviewed);
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--expect requires --apply"));
    result = RUNV("bound", "--apply", "--expect", "md5:00");
    CHECK(result.code == 1 && bound_writes == 0 && strstr(result.err, "[VALIDATION_FAILED]"));
    release(&result);
    /* The state moves between the review and the application: refused
     * before anything is written, with the code that says nothing changed. */
    bound_state = 8;
    result = RUNV("bound", "--apply", "--expect", reviewed);
    CHECK(result.code == 1 && bound_writes == 0 && bound_state == 8 && result.out[0] == '\0' &&
        strstr(result.err, "[PRECONDITION_FAILED]") && strstr(result.err, "Plan again without --apply"));
    release(&result);
    /* The reviewed plan is still the one: applied, and the result carries
     * the fingerprint of the action performed. */
    bound_state = 7;
    result = RUNV("bound", "--apply", "--expect", reviewed, "--json", "--compact");
    CHECK(result.code == 0 && bound_writes == 1 && bound_state == 8 &&
        strstr(result.out, "\"mode\":\"apply\"") && strstr(result.out, reviewed));
    release(&result);
    /* A caller that never passes --expect is served as before. */
    result = RUNV("bound", "--apply");
    CHECK(result.code == 0 && bound_writes == 2);
    release(&result);
    /* A handler that answers without having checked is not believed. */
    result = RUNV("careless", "--apply", "--expect", reviewed);
    CHECK(result.code == 1 && result.out[0] == '\0' && strstr(result.err, "[UNEXPECTED]") &&
        strstr(result.err, "answered without checking --expect"));
    release(&result);
    result = RUNV("careless", "--apply");
    CHECK(result.code == 0);
    release(&result);
    result = RUNV("describe", "bound", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"long\":\"--expect\"") &&
        strstr(result.out, "\"algorithms\":[\"sha256\"]") && strstr(result.out, "\"requires\":[\"--apply\"]"));
    release(&result);
    return 1;
}

static int test_environment_format(void) {
    CHECK(setenv("MAELYS_CLI_FORMAT", "json", 1) == 0);
    run_result_t result = RUNV("report");
    CHECK(result.code == 2 && strstr(result.out, "\"command\":\"report\",\"ok\":true") == NULL);
    CHECK(strstr(result.out, "\"ok\": true") != NULL); /* pretty JSON by default */
    release(&result);
    result = RUNV("exec", "cmd", "--", "x");
    CHECK(result.code == 7 && strcmp(result.out, "2\n") == 0); /* stream stdout untouched */
    release(&result);
    result = RUNV("thing", "make");
    CHECK(result.code == 1 && strstr(result.err, "\"code\": \"VALIDATION_FAILED\""));
    release(&result);
    result = RUNV("report", "--format", "text");
    CHECK(result.code == 2 && strcmp(result.out, "invalid\n") == 0);
    release(&result);
    /* The environment's format is a default: --compact selects none and
     * leaves it in force, where 0.5.35 answered text. */
    result = RUNV("report", "--compact");
    CHECK(result.code == 2 && strstr(result.out, "\"command\":\"report\",\"ok\":true"));
    release(&result);
    /* --field against the format the environment selected is a rendering
     * refusal like the explicit one, and comes before the command runs: a
     * transaction is not applied and then refused. */
    mirror_runs = 0;
    result = RUNV("mirror", "--apply", "--source-oid", "abcd", "--target-oid", "ef01", "--field", "mode");
    CHECK(result.code == 1 && mirror_runs == 0 && result.out[0] == '\0' &&
        strstr(result.err, "--field conflicts with --format json"));
    release(&result);
    result = RUNV("mirror", "--apply", "--source-oid", "abcd", "--target-oid", "ef01", "--field", "mode", "--compact");
    CHECK(result.code == 1 && mirror_runs == 0 &&
        strstr(result.err, "\"code\":\"VALIDATION_FAILED\""));
    release(&result);
    CHECK(unsetenv("MAELYS_CLI_FORMAT") == 0);
    /* A command that can write accepts for --field only a member its output
     * schema requires, and says so before it runs (spec 2.9, section 5).
     * `mirror` is a transaction whose schema requires none: no name is
     * accepted, with or without --apply. */
    result = RUNV("mirror", "--field", "mode");
    CHECK(result.code == 1 && mirror_runs == 0 && result.out[0] == '\0' &&
        strstr(result.err, "[VALIDATION_FAILED]") &&
        strstr(result.err, "its output schema requires no member"));
    release(&result);
    result = RUNV("mirror", "--apply", "--source-oid", "abcd", "--target-oid", "ef01", "--field", "mode");
    CHECK(result.code == 1 && mirror_runs == 0);
    release(&result);
    /* `thing make` requires "mode": that name is accepted and the command
     * runs; another is refused while nothing has run, where 0.5.35 ran the
     * command and then answered that data did not have it. */
    make_runs = 0;
    result = RUNV("thing", "make", "/root", "name", "--field", "mode");
    CHECK(result.code == 0 && make_runs == 1 && strcmp(result.out, "plan\n") == 0);
    release(&result);
    result = RUNV("thing", "make", "/root", "name", "--apply", "--field", "path");
    CHECK(result.code == 1 && make_runs == 1 && result.out[0] == '\0' &&
        strstr(result.err, "names 'path', which 'thing.make' does not always return"));
    release(&result);
    /* A read decides on data, as before: the refusal costs nothing there. */
    result = RUNV("describe", "--field", "no-such-member");
    CHECK(result.code == 1 && strstr(result.err, "which 'describe' does not have"));
    release(&result);
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_object(&writer) == 0);
    CHECK(maelys_cli_json_key_string(&writer, "x", NULL) != 0);
    CHECK(maelys_cli_json_finish(&writer) == NULL);
    return 1;
}

/* Runs one broken catalog through maelys_cli_run and returns 1 when it is
 * refused at startup with an [UNEXPECTED] naming `detail`. */
static int refused_at_startup(const maelys_cli_command_t *broken, const char *detail) {
    maelys_cli_app_t bad = app;
    bad.commands = broken;
    bad.command_count = 1u;
    char *out = NULL;
    char *err = NULL;
    size_t out_size = 0u;
    size_t err_size = 0u;
    FILE *out_stream = open_memstream(&out, &out_size);
    FILE *err_stream = open_memstream(&err, &err_size);
    int code = maelys_cli_run(&bad, 0, NULL, out_stream, err_stream);
    (void)fclose(out_stream);
    (void)fclose(err_stream);
    int refused = code == 1 && !out[0] && strstr(err, "[UNEXPECTED]") && strstr(err, detail);
    free(out);
    free(err);
    return refused;
}

static int test_operand_pattern(void) {
    /* describe states the operand's value as it states an argument's. */
    run_result_t result = RUNV("describe", "named", "--json", "--compact");
    CHECK(result.code == 0);
    CHECK(strstr(result.out, "\"name\":\"LABEL\",\"required\":true,\"variadic\":false,\"summary\":\"A matched label.\",\"type\":\"string\",\"pattern\":\"^[a-z][a-z0-9-]*$\""));
    CHECK(strstr(result.out, "\"name\":\"SUM\",\"required\":false,\"variadic\":false,\"summary\":\"A fixed-width hex sum.\",\"type\":\"hex\",\"digits\":8"));
    release(&result);
    /* The parser enforces it as it enforces an option's, in the operand's
     * own wording. */
    result = RUNV("named", "Label-1");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operand LABEL expects"));
    result = RUNV("named", "label-1");
    CHECK(result.code == 0);
    release(&result);
    result = RUNV("named", "label-1", "deadbeef");
    CHECK(result.code == 0);
    release(&result);
    result = RUNV("named", "label-1", "dead");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "Operand SUM expects"));
    /* Refused at startup as an option's would be: a pattern on a kind that
     * is not string or path, and a pattern that does not compile. */
    maelys_cli_operand_t operands[2];
    memcpy(operands, named_operands, sizeof(operands));
    maelys_cli_command_t broken = commands[0];
    for (size_t i = 0u; i < MAELYS_CLI_COUNT(commands); ++i)
        if (!strcmp(commands[i].id, "named")) broken = commands[i];
    broken.operands = operands;
    operands[1].pattern = "^[0-9a-f]+$";
    CHECK(refused_at_startup(&broken, "operand SUM declares a pattern on a kind"));
    operands[1].pattern = NULL;
    operands[0].pattern = "^[a-z";
    CHECK(refused_at_startup(&broken, "operand LABEL declares a pattern that is not"));

    /* An unavailable code must be one of the stable codes, and needs a
     * reason: a catalog cannot invent a code an agent would read. */
    maelys_cli_command_t invented[] = {
        {MAELYS_CLI_READ("sealed", "sealed", "Sealed.", NULL),
         .unavailable = "refused", .unavailable_code = "DIGEST_MISMATCH"},
    };
    CHECK(refused_at_startup(invented, "is not one of the stable codes"));
    maelys_cli_command_t codeless[] = {
        {MAELYS_CLI_READ("sealed", "sealed", "Sealed.", command_report),
         .unavailable_code = MAELYS_CLI_CODE_ACCESS_DENIED},
    };
    CHECK(refused_at_startup(codeless, "names an unavailable code without an"));
    return 1;
}

static int test_constraints(void) {
    /* describe states the three rules after the derived entries, in the
     * contract's shape and without a name. */
    run_result_t result = RUNV("describe", "policy", "--json", "--compact");
    CHECK(result.code == 0);
    CHECK(strstr(result.out, "\"kind\":\"exactly-one\",\"options\":[\"--ro\",\"--rw\",\"--plan\"]"));
    CHECK(strstr(result.out, "\"kind\":\"at-most-one\",\"options\":[\"--trace\",\"--quiet\"]"));
    CHECK(strstr(result.out, "\"kind\":\"requires\",\"options\":[\"--verbose-trace\",\"--trace\",\"--plan\"]"));
    release(&result);

    /* exactly-one refuses zero as it refuses two; one passes. */
    result = RUNV("policy");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]",
        "Exactly one of --ro, --rw, --plan must be given."));
    result = RUNV("policy", "--ro", "--rw");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]",
        "Exactly one of --ro, --rw, --plan must be given."));
    result = RUNV("policy", "--rw");
    CHECK(result.code == 0);
    release(&result);
    /* at-most-one accepts one and none, refuses two. */
    result = RUNV("policy", "--ro", "--quiet");
    CHECK(result.code == 0);
    release(&result);
    result = RUNV("policy", "--ro", "--trace", "--quiet");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]",
        "At most one of --trace, --quiet may be given."));
    /* requires: the first option requires every other. */
    result = RUNV("policy", "--plan", "p", "--verbose-trace");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]",
        "--verbose-trace requires --trace."));
    result = RUNV("policy", "--plan", "p", "--verbose-trace", "--trace");
    CHECK(result.code == 0);
    release(&result);
    /* The stated rule is refused in the causal slot of the dependencies:
     * before a missing required option would be, after a bad value. */
    result = RUNV("policy", "--plan", "");
    CHECK(expect_failure(&result, "[VALIDATION_FAILED]", "--plan"));

    /* The catalog validation holds the declaration to one form per rule
     * and to the command's own options. */
    static const char *const one[] = {"ro", NULL};
    static const char *const stranger[] = {"ro", "elsewhere", NULL};
    static const char *const twice[] = {"ro", "ro", NULL};
    maelys_cli_constraint_t rule = {MAELYS_CLI_CONSTRAINT(
        MAELYS_CLI_CONSTRAINT_ALL_OR_NONE, policy_sources)};
    maelys_cli_command_t broken = commands[0];
    for (size_t i = 0u; i < MAELYS_CLI_COUNT(commands); ++i)
        if (!strcmp(commands[i].id, "policy")) broken = commands[i];
    broken.constraints = &rule;
    broken.constraint_count = 1u;
    CHECK(refused_at_startup(&broken, "declare .group on its options instead"));
    rule.kind = MAELYS_CLI_CONSTRAINT_EXACTLY_ONE;
    rule.options = one;
    CHECK(refused_at_startup(&broken, "fewer than two options"));
    rule.options = stranger;
    CHECK(refused_at_startup(&broken, "names unknown option --elsewhere"));
    rule.options = twice;
    CHECK(refused_at_startup(&broken, "names --ro twice"));
    return 1;
}

static int test_invalid_catalog(void) {
    maelys_cli_command_t broken = commands[0];
    broken.id = "Broken Id";
    maelys_cli_app_t bad = app;
    bad.commands = &broken;
    bad.command_count = 1u;
    char *out = NULL;
    char *err = NULL;
    size_t out_size = 0u;
    size_t err_size = 0u;
    FILE *out_stream = open_memstream(&out, &out_size);
    FILE *err_stream = open_memstream(&err, &err_size);
    int code = maelys_cli_run(&bad, 0, NULL, out_stream, err_stream);
    (void)fclose(out_stream);
    (void)fclose(err_stream);
    CHECK(code == 1 && !out[0] && strstr(err, "[UNEXPECTED]") && strstr(err, "invalid identifier"));
    free(out);
    free(err);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_help_and_version);
    RUN(test_describe);
    RUN(test_parsing_success);
    RUN(test_parsing_failures);
    RUN(test_stream_records_and_codes);
    RUN(test_field);
    RUN(test_typed_operands_and_completion);
    RUN(test_expect);
    RUN(test_groups_defaults_unavailable);
    RUN(test_constraints);
    RUN(test_operand_pattern);
    RUN(test_environment_format);
    RUN(test_invalid_catalog);
    return failures ? 1 : 0;
}
