#ifndef MAELYS_CLI_VALUES_H
#define MAELYS_CLI_VALUES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Product-neutral value parsing. Every parser rejects empty strings, signs
 * where they are not meaningful, trailing garbage, overflow and values
 * outside the inclusive [minimum, maximum] range. All return 0 on success and
 * -1 on refusal without touching the output.
 */

int maelys_cli_parse_u64_decimal(
    const char *value, uint64_t minimum, uint64_t maximum,
    uint64_t *out_value);
int maelys_cli_parse_u32_decimal(
    const char *value, uint32_t minimum, uint32_t maximum,
    uint32_t *out_value);
int maelys_cli_parse_i64_decimal(
    const char *value, int64_t minimum, int64_t maximum,
    int64_t *out_value);

/* Bytes with an optional single K, M, G or T suffix (powers of 1024). */
int maelys_cli_parse_byte_size(
    const char *value, uint64_t minimum, uint64_t maximum,
    uint64_t *out_bytes);

/* Milliseconds from a decimal number with a mandatory unit suffix:
 * ms, s, m, h or d. A bare number is refused to avoid unit ambiguity. */
int maelys_cli_parse_duration_ms(
    const char *value, uint64_t minimum, uint64_t maximum,
    uint64_t *out_milliseconds);

/* Accepts true/false, yes/no, on/off and 1/0. */
int maelys_cli_parse_boolean(const char *value, int *out_value);

/* Exact match against a NULL-terminated array of allowed spellings. */
int maelys_cli_parse_choice(
    const char *value, const char *const *choices, size_t *out_index);

/* Lowercase hexadecimal of exactly the given digit count. */
int maelys_cli_parse_hex(const char *value, size_t digit_count);

typedef struct maelys_cli_string_list {
    const char **items; /* borrowed string views */
    size_t count;
} maelys_cli_string_list_t;

int maelys_cli_string_list_append(
    maelys_cli_string_list_t *values, const char *value);
void maelys_cli_string_list_clear(maelys_cli_string_list_t *values);

#ifdef __cplusplus
}
#endif

#endif
