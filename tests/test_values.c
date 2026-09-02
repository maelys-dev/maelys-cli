#include "check.h"

#include <maelys/cli/values.h>

#include <string.h>

static int test_unsigned(void) {
    uint64_t wide = 1u;
    uint32_t narrow = 1u;
    CHECK(maelys_cli_parse_u64_decimal("0", 0u, UINT64_MAX, &wide) == 0 && wide == 0u);
    CHECK(maelys_cli_parse_u64_decimal("18446744073709551615", 0u, UINT64_MAX, &wide) == 0);
    CHECK(wide == UINT64_MAX);
    CHECK(maelys_cli_parse_u64_decimal("18446744073709551616", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("-1", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("+1", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal(" 1", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("1 ", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("0x10", 0u, UINT64_MAX, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("5", 6u, 10u, &wide) != 0);
    CHECK(maelys_cli_parse_u64_decimal("5", 6u, 5u, &wide) != 0);
    CHECK(maelys_cli_parse_u32_decimal("42", 1u, 100u, &narrow) == 0 && narrow == 42u);
    CHECK(maelys_cli_parse_u32_decimal("101", 1u, 100u, &narrow) != 0);
    CHECK(maelys_cli_parse_u32_decimal("4294967296", 0u, UINT32_MAX, &narrow) != 0);
    return 1;
}

static int test_signed(void) {
    int64_t value = 0;
    CHECK(maelys_cli_parse_i64_decimal("-42", -100, 100, &value) == 0 && value == -42);
    CHECK(maelys_cli_parse_i64_decimal("100", -100, 100, &value) == 0 && value == 100);
    CHECK(maelys_cli_parse_i64_decimal("101", -100, 100, &value) != 0);
    CHECK(maelys_cli_parse_i64_decimal("-", -100, 100, &value) != 0);
    CHECK(maelys_cli_parse_i64_decimal("--1", -100, 100, &value) != 0);
    CHECK(maelys_cli_parse_i64_decimal("-9223372036854775808", INT64_MIN, INT64_MAX, &value) == 0);
    CHECK(value == INT64_MIN);
    CHECK(maelys_cli_parse_i64_decimal("9223372036854775808", INT64_MIN, INT64_MAX, &value) != 0);
    return 1;
}

static int test_sizes(void) {
    uint64_t bytes = 0u;
    CHECK(maelys_cli_parse_byte_size("16M", 1u, UINT64_MAX, &bytes) == 0);
    CHECK(bytes == UINT64_C(16) * 1024u * 1024u);
    CHECK(maelys_cli_parse_byte_size("2k", 1u, UINT64_MAX, &bytes) == 0 && bytes == 2048u);
    CHECK(maelys_cli_parse_byte_size("1G", 1u, UINT64_MAX, &bytes) == 0 && bytes == (UINT64_C(1) << 30));
    CHECK(maelys_cli_parse_byte_size("1T", 1u, UINT64_MAX, &bytes) == 0 && bytes == (UINT64_C(1) << 40));
    CHECK(maelys_cli_parse_byte_size("0", 0u, UINT64_MAX, &bytes) == 0 && bytes == 0u);
    CHECK(maelys_cli_parse_byte_size("0", 1u, UINT64_MAX, &bytes) != 0);
    CHECK(maelys_cli_parse_byte_size("1P", 1u, UINT64_MAX, &bytes) != 0);
    CHECK(maelys_cli_parse_byte_size("1MB", 1u, UINT64_MAX, &bytes) != 0);
    CHECK(maelys_cli_parse_byte_size("16777216T", 1u, UINT64_MAX, &bytes) != 0);
    CHECK(maelys_cli_parse_byte_size("-1M", 0u, UINT64_MAX, &bytes) != 0);
    CHECK(maelys_cli_parse_byte_size("M", 0u, UINT64_MAX, &bytes) != 0);
    return 1;
}

static int test_durations(void) {
    uint64_t ms = 0u;
    CHECK(maelys_cli_parse_duration_ms("500ms", 0u, UINT64_MAX, &ms) == 0 && ms == 500u);
    CHECK(maelys_cli_parse_duration_ms("30s", 0u, UINT64_MAX, &ms) == 0 && ms == 30000u);
    CHECK(maelys_cli_parse_duration_ms("5m", 0u, UINT64_MAX, &ms) == 0 && ms == 300000u);
    CHECK(maelys_cli_parse_duration_ms("2h", 0u, UINT64_MAX, &ms) == 0 && ms == 7200000u);
    CHECK(maelys_cli_parse_duration_ms("1d", 0u, UINT64_MAX, &ms) == 0 && ms == 86400000u);
    CHECK(maelys_cli_parse_duration_ms("10", 0u, UINT64_MAX, &ms) != 0);
    CHECK(maelys_cli_parse_duration_ms("10x", 0u, UINT64_MAX, &ms) != 0);
    CHECK(maelys_cli_parse_duration_ms("1s", 2000u, UINT64_MAX, &ms) != 0);
    CHECK(maelys_cli_parse_duration_ms("999999999999999999d", 0u, UINT64_MAX, &ms) != 0);
    return 1;
}

static int test_booleans_choices_hex(void) {
    int flag = -1;
    size_t index = 99u;
    static const char *const choices[] = {"auto", "fd4", "proxy", NULL};
    CHECK(maelys_cli_parse_boolean("true", &flag) == 0 && flag == 1);
    CHECK(maelys_cli_parse_boolean("off", &flag) == 0 && flag == 0);
    CHECK(maelys_cli_parse_boolean("maybe", &flag) != 0);
    CHECK(maelys_cli_parse_choice("proxy", choices, &index) == 0 && index == 2u);
    CHECK(maelys_cli_parse_choice("Proxy", choices, &index) != 0);
    CHECK(maelys_cli_parse_hex("0123456789abcdef", 16u) == 0);
    CHECK(maelys_cli_parse_hex("0123456789ABCDEF", 16u) != 0);
    CHECK(maelys_cli_parse_hex("0123", 16u) != 0);
    return 1;
}

static int test_string_list(void) {
    maelys_cli_string_list_t values = {0};
    CHECK(maelys_cli_string_list_append(&values, "one") == 0);
    CHECK(maelys_cli_string_list_append(&values, "two") == 0);
    CHECK(values.count == 2u && strcmp(values.items[1], "two") == 0);
    CHECK(maelys_cli_string_list_append(&values, NULL) != 0);
    maelys_cli_string_list_clear(&values);
    CHECK(values.items == NULL && values.count == 0u);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_unsigned);
    RUN(test_signed);
    RUN(test_sizes);
    RUN(test_durations);
    RUN(test_booleans_choices_hex);
    RUN(test_string_list);
    return failures ? 1 : 0;
}
