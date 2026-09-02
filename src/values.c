#include "maelys/cli/values.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int all_digits(const char *value) {
    if (!value || !*value) return 0;
    for (const char *p = value; *p; ++p)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

static int parse_unsigned_prefix(
    const char *value, unsigned long long *out, const char **out_end) {
    if (!value || !*value || *value == '-' || *value == '+' ||
        *value < '0' || *value > '9')
        return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value) return -1;
    *out = parsed;
    *out_end = end;
    return 0;
}

int maelys_cli_parse_u64_decimal(
    const char *value, uint64_t minimum, uint64_t maximum,
    uint64_t *out_value) {
    unsigned long long parsed = 0ull;
    const char *end = NULL;
    if (!out_value || minimum > maximum || !all_digits(value) ||
        parse_unsigned_prefix(value, &parsed, &end) != 0 || *end != '\0' ||
        parsed > UINT64_MAX || (uint64_t)parsed < minimum ||
        (uint64_t)parsed > maximum)
        return -1;
    *out_value = (uint64_t)parsed;
    return 0;
}

int maelys_cli_parse_u32_decimal(
    const char *value, uint32_t minimum, uint32_t maximum,
    uint32_t *out_value) {
    uint64_t parsed = 0u;
    if (!out_value || minimum > maximum ||
        maelys_cli_parse_u64_decimal(value, minimum, maximum, &parsed) != 0)
        return -1;
    *out_value = (uint32_t)parsed;
    return 0;
}

int maelys_cli_parse_i64_decimal(
    const char *value, int64_t minimum, int64_t maximum,
    int64_t *out_value) {
    if (!value || !*value || !out_value || minimum > maximum) return -1;
    const char *digits = value;
    if (*digits == '-') ++digits;
    if (!all_digits(digits)) return -1;
    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < minimum || parsed > maximum)
        return -1;
    *out_value = (int64_t)parsed;
    return 0;
}

int maelys_cli_parse_byte_size(
    const char *value, uint64_t minimum, uint64_t maximum,
    uint64_t *out_bytes) {
    unsigned long long raw = 0ull;
    const char *end = NULL;
    if (!out_bytes || minimum > maximum ||
        parse_unsigned_prefix(value, &raw, &end) != 0)
        return -1;
    uint64_t multiplier = 1u;
    if (*end) {
        if (end[1] != '\0') return -1;
        switch (*end) {
            case 'k': case 'K': multiplier = UINT64_C(1) << 10; break;
            case 'm': case 'M': multiplier = UINT64_C(1) << 20; break;
            case 'g': case 'G': multiplier = UINT64_C(1) << 30; break;
            case 't': case 'T': multiplier = UINT64_C(1) << 40; break;
            default: return -1;
        }
    }
    if ((uint64_t)raw > UINT64_MAX / multiplier) return -1;
    uint64_t parsed = (uint64_t)raw * multiplier;
    if (parsed < minimum || parsed > maximum) return -1;
    *out_bytes = parsed;
    return 0;
}

int maelys_cli_parse_duration_ms(
    const char *value, uint64_t minimum, uint64_t maximum,
    uint64_t *out_milliseconds) {
    unsigned long long raw = 0ull;
    const char *end = NULL;
    if (!out_milliseconds || minimum > maximum ||
        parse_unsigned_prefix(value, &raw, &end) != 0 || !*end)
        return -1;
    uint64_t multiplier;
    if (strcmp(end, "ms") == 0) multiplier = 1u;
    else if (strcmp(end, "s") == 0) multiplier = 1000u;
    else if (strcmp(end, "m") == 0) multiplier = 60u * 1000u;
    else if (strcmp(end, "h") == 0) multiplier = 60u * 60u * 1000u;
    else if (strcmp(end, "d") == 0) multiplier = 24u * 60u * 60u * 1000u;
    else return -1;
    if ((uint64_t)raw > UINT64_MAX / multiplier) return -1;
    uint64_t parsed = (uint64_t)raw * multiplier;
    if (parsed < minimum || parsed > maximum) return -1;
    *out_milliseconds = parsed;
    return 0;
}

int maelys_cli_parse_boolean(const char *value, int *out_value) {
    if (!value || !out_value) return -1;
    if (!strcmp(value, "true") || !strcmp(value, "yes") ||
        !strcmp(value, "on") || !strcmp(value, "1")) {
        *out_value = 1;
        return 0;
    }
    if (!strcmp(value, "false") || !strcmp(value, "no") ||
        !strcmp(value, "off") || !strcmp(value, "0")) {
        *out_value = 0;
        return 0;
    }
    return -1;
}

int maelys_cli_parse_choice(
    const char *value, const char *const *choices, size_t *out_index) {
    if (!value || !choices || !out_index) return -1;
    for (size_t i = 0u; choices[i]; ++i) {
        if (strcmp(choices[i], value) == 0) {
            *out_index = i;
            return 0;
        }
    }
    return -1;
}

int maelys_cli_parse_hex(const char *value, size_t digit_count) {
    if (!value || digit_count == 0u || strlen(value) != digit_count) return -1;
    for (const char *p = value; *p; ++p) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
            return -1;
    }
    return 0;
}

int maelys_cli_string_list_append(
    maelys_cli_string_list_t *values, const char *value) {
    if (!values || !value ||
        values->count >= SIZE_MAX / sizeof(*values->items) - 1u)
        return -1;
    const char **grown = realloc(
        values->items, (values->count + 1u) * sizeof(*values->items));
    if (!grown) return -1;
    values->items = grown;
    values->items[values->count++] = value;
    return 0;
}

void maelys_cli_string_list_clear(maelys_cli_string_list_t *values) {
    if (!values) return;
    free(values->items);
    values->items = NULL;
    values->count = 0u;
}
