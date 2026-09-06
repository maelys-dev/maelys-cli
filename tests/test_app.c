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

static int command_make(maelys_cli_context_t *context) {
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

static int command_mirror(maelys_cli_context_t *context) {
    mirror_retries = 99u;
    mirror_retries_delivered = maelys_cli_option_unsigned(context, "retries", &mirror_retries);
    (void)maelys_cli_option_choice(context, "mode", &mirror_mode);
    return maelys_cli_succeed(context, "{}", maelys_cli_option_or(context, "mode", "?"),
        MAELYS_CLI_EXIT_OK);
}

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

static const maelys_cli_command_t commands[] = {
    {MAELYS_CLI_TRANSACTION("thing.make", "thing make", "Make.", command_make),
     MAELYS_CLI_OPERANDS(make_operands), MAELYS_CLI_OPTIONS(make_options),
     MAELYS_CLI_SCHEMA("{\"type\":\"object\",\"required\":[\"mode\"]}")},
    {MAELYS_CLI_PROTOCOL_STREAM("exec", "exec", "Exec.", command_exec, "test-jsonl"),
     MAELYS_CLI_OPERANDS(rest_operands)},
    {MAELYS_CLI_RECORDS("records", "records", "Records.", command_records)},
    {MAELYS_CLI_READ("report", "report", "Report.", command_report)},
    {MAELYS_CLI_READ("typed", "typed", "Typed operands.", command_typed),
     MAELYS_CLI_OPERANDS(typed_operands)},
    {MAELYS_CLI_TRANSACTION("mirror", "mirror", "Mirror.", command_mirror),
     MAELYS_CLI_OPTIONS(mirror_options)},
    {MAELYS_CLI_READ("trusted", "trusted", "Trusted output.", command_trusted),
     MAELYS_CLI_OPTIONS(trusted_options), .hidden = 1},
    {MAELYS_CLI_RECORDS("trusted-records", "trusted-records", "Trusted records.",
     command_trusted), .hidden = 1},
    {.id = "cloud", .pattern = "cloud", .purpose = "Cloud sync.",
     .effect = MAELYS_CLI_EFFECT_EXECUTE, .output = MAELYS_CLI_OUTPUT_ENVELOPE,
     .unavailable = "built without the Cloud agent"},
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
    MAELYS_CLI_COUNT(commands), NULL, 0u, "EXTRA GUIDANCE", NULL
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

static int test_help_and_version(void) {
    run_result_t result = run(0, NULL);
    CHECK(result.code == 0 && strstr(result.out, "COMMANDS") && !result.err[0]);
    CHECK(strstr(result.out, "thing make ROOT NAME") && strstr(result.out, "EXTRA GUIDANCE"));
    CHECK(!strstr(result.out, "badjson")); /* hidden */
    release(&result);
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
    CHECK(strstr(result.out, "\"digits\":4,\"alternativeDigits\":8"));
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
    CHECK(result.code == 0 && strstr(result.out, "complete.candidates") == NULL &&
        strstr(result.out, "cloud\n") != NULL);
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
    CHECK(strstr(result.out, "\"kind\":\"all-or-none\",\"group\":\"preconditions\",\"options\":[\"--source-oid\",\"--target-oid\"]"));
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
    CHECK(unsetenv("MAELYS_CLI_FORMAT") == 0);
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_object(&writer) == 0);
    CHECK(maelys_cli_json_key_string(&writer, "x", NULL) != 0);
    CHECK(maelys_cli_json_finish(&writer) == NULL);
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
    RUN(test_typed_operands_and_completion);
    RUN(test_groups_defaults_unavailable);
    RUN(test_environment_format);
    RUN(test_invalid_catalog);
    return failures ? 1 : 0;
}
