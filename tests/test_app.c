#include "check.h"

#include <maelys/cli.h>

#include <stdlib.h>
#include <string.h>

/* ---- fixture catalog ------------------------------------------------------- */

static const char *const levels[] = {"low", "high", NULL};
static const maelys_cli_operand_t make_operands[] = {
    {MAELYS_CLI_OPERAND("ROOT", "Root.")},
    {MAELYS_CLI_OPERAND("NAME", "Name.")},
};
static const maelys_cli_option_t make_options[] = {
    {MAELYS_CLI_PATH("git", "FILE", "Git path.")},
    {MAELYS_CLI_CHOICE("level", "Level.", levels), .default_text = "low"},
    {MAELYS_CLI_SIZE("memory", "BYTES", "Memory.", 1u, 0u)},
    {MAELYS_CLI_DURATION("wait", "DURATION", "Wait.", 0u, 0u)},
    {MAELYS_CLI_INTEGER("offset", "N", "Offset.", -10, 10)},
    {MAELYS_CLI_STRING("tag", "TEXT", "Tag."), .repeatable = 1},
    {MAELYS_CLI_HEX_OR("oid", "OID", "Object id.", 4u, 8u)},
    {MAELYS_CLI_FLAG("strict", "Strict."), .depends_on = "git"},
    {MAELYS_CLI_FLAG("lenient", "Lenient."), .conflicts_with = "strict"},
    MAELYS_CLI_APPLY_OPTION,
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
    release(&result);
    result = RUNV("describe", "thing.make", "--json", "--compact");
    CHECK(result.code == 0 && strstr(result.out, "\"kind\":\"command\""));
    CHECK(strstr(result.out, "\"outputSchema\":{\"type\":\"object\",\"required\":[\"mode\"]}"));
    CHECK(strstr(result.out, "\"choices\":[\"low\",\"high\"]") && strstr(result.out, "\"default\":\"low\""));
    CHECK(strstr(result.out, "\"minimum\":-10,\"maximum\":10"));
    CHECK(strstr(result.out, "\"digits\":4,\"alternativeDigits\":8"));
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
    result = RUNV("thing", "make", "/root", "name", "--oid", "0123abcd", "--json", "--compact");
    CHECK(result.code == 0 && !result.err[0]);
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
    result = RUNV("records");
    CHECK(result.code == 0 && strcmp(result.out, "zero\none\n") == 0);
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
    RUN(test_invalid_catalog);
    return failures ? 1 : 0;
}
