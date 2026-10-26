#include "check.h"

#include <maelys/cli/json.h>

#include <stdlib.h>
#include <string.h>

static int test_writer(void) {
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_object(&writer) == 0);
    CHECK(maelys_cli_json_key_string(&writer, "name", "va\"lue\n\t\x01") == 0);
    CHECK(maelys_cli_json_key_integer(&writer, "signed", -5) == 0);
    CHECK(maelys_cli_json_key_unsigned(&writer, "unsigned", UINT64_MAX) == 0);
    CHECK(maelys_cli_json_key_boolean(&writer, "flag", 1) == 0);
    CHECK(maelys_cli_json_key(&writer, "list") == 0);
    CHECK(maelys_cli_json_begin_array(&writer) == 0);
    CHECK(maelys_cli_json_null(&writer) == 0);
    CHECK(maelys_cli_json_string(&writer, "x") == 0);
    CHECK(maelys_cli_json_raw(&writer, " {\"nested\": [1, 2]} ") == 0);
    CHECK(maelys_cli_json_string(&writer, NULL) != 0 && writer.failed);
    maelys_cli_json_writer_clear(&writer);
    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_object(&writer) == 0);
    CHECK(maelys_cli_json_key_string(&writer, "name", "va\"lue\n\t\x01") == 0);
    CHECK(maelys_cli_json_key_integer(&writer, "signed", -5) == 0);
    CHECK(maelys_cli_json_key_unsigned(&writer, "unsigned", UINT64_MAX) == 0);
    CHECK(maelys_cli_json_key_boolean(&writer, "flag", 1) == 0);
    CHECK(maelys_cli_json_key(&writer, "list") == 0);
    CHECK(maelys_cli_json_begin_array(&writer) == 0);
    CHECK(maelys_cli_json_null(&writer) == 0);
    CHECK(maelys_cli_json_string(&writer, "x") == 0);
    CHECK(maelys_cli_json_raw(&writer, " {\"nested\": [1, 2]} ") == 0);
    CHECK(maelys_cli_json_end_array(&writer) == 0);
    CHECK(maelys_cli_json_key_raw(&writer, "empty", "{}") == 0);
    CHECK(maelys_cli_json_end_object(&writer) == 0);
    char *text = maelys_cli_json_finish(&writer);
    CHECK(text != NULL);
    CHECK(strcmp(text,
        "{\"name\":\"va\\\"lue\\n\\t\\u0001\",\"signed\":-5,"
        "\"unsigned\":18446744073709551615,\"flag\":true,"
        "\"list\":[null,\"x\",{\"nested\": [1, 2]}],\"empty\":{}}") == 0);
    free(text);

    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_object(&writer) == 0);
    CHECK(maelys_cli_json_string(&writer, "no key") != 0);
    CHECK(maelys_cli_json_finish(&writer) == NULL);

    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_array(&writer) == 0);
    CHECK(maelys_cli_json_raw(&writer, "{invalid") != 0);
    CHECK(maelys_cli_json_finish(&writer) == NULL);

    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_begin_object(&writer) == 0);
    CHECK(maelys_cli_json_finish(&writer) == NULL); /* unclosed */
    return 1;
}

static int test_writer_utf8(void) {
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    CHECK(maelys_cli_json_string(&writer, "caf\xc3\xa9 \xf0\x9f\x98\x80") == 0);
    char *text = maelys_cli_json_finish(&writer);
    CHECK(text && strcmp(text, "\"caf\xc3\xa9 \xf0\x9f\x98\x80\"") == 0);
    free(text);
    static const char *const bad[] = {
        "\xc3", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "a\xff",
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        maelys_cli_json_writer_init(&writer);
        CHECK(maelys_cli_json_string(&writer, bad[i]) != 0 && writer.failed);
        maelys_cli_json_writer_clear(&writer);
    }
    return 1;
}

static int test_validate(void) {
    static const char *const valid[] = {
        "{}", "[]", "0", "-1.5e+3", "\"\\u00e9\\n\"", "true", " null ",
        "{\"a\":[1,{\"b\":null}],\"c\":\"\\\"\"}",
    };
    static const char *const invalid[] = {
        "", "{", "}", "{\"a\":}", "{a:1}", "[1,]", "01", "1.", ".5", "-",
        "\"unterminated", "\"tab\there\"", "\"\\x\"", "\"\\u12\"", "tru",
        "{} {}", "[1 2]", "{\"a\":1,}", "nul",
    };
    for (size_t i = 0u; i < sizeof(valid) / sizeof(valid[0]); ++i)
        CHECK(maelys_cli_json_validate(valid[i], strlen(valid[i]), NULL) == 0);
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(maelys_cli_json_validate(invalid[i], strlen(invalid[i]), NULL) != 0);
    char deep[200];
    memset(deep, '[', 100u);
    memset(deep + 100, ']', 100u);
    deep[199] = '\0';
    CHECK(maelys_cli_json_validate(deep, strlen(deep), NULL) != 0);
    size_t offset = 0u;
    CHECK(maelys_cli_json_validate("[1, x]", 6u, &offset) != 0 && offset == 4u);
    return 1;
}

static int test_format(void) {
    char *text = NULL;
    CHECK(maelys_cli_json_format(" { \"a\" : [ 1 , { \"b\" : null } ] , \"c\" : {} } ",
        1, &text) == 0);
    CHECK(strcmp(text, "{\"a\":[1,{\"b\":null}],\"c\":{}}") == 0);
    free(text);
    CHECK(maelys_cli_json_format("{\"a\":[1,{\"b\":null}],\"c\":{}}", 0, &text) == 0);
    CHECK(strcmp(text,
        "{\n  \"a\": [\n    1,\n    {\n      \"b\": null\n    }\n  ],\n"
        "  \"c\": {}\n}") == 0);
    free(text);
    CHECK(maelys_cli_json_format("{bad}", 0, &text) != 0);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_writer);
    RUN(test_validate);
    RUN(test_writer_utf8);
    RUN(test_format);
    return failures ? 1 : 0;
}
