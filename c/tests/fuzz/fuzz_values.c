/* SPDX-License-Identifier: MPL-2.0 */
/* The value parsers of values.h on arbitrary text, with bounds taken from
 * the input, and the properties a caller relies on: a parser that accepts
 * a value returns one inside the bounds it was given, and refuses every
 * value when the bounds are inverted.
 *
 * Input: one selector byte, eight bytes of first bound, eight of second
 * bound, then the text (NUL-terminated here, as argv words are). */
#include "maelys/cli/values.h"
#include "replay.h"

#include <stdlib.h>
#include <string.h>

#define VALUES_MAX_BYTES 1024u

static uint64_t read_u64(const uint8_t *bytes) {
    uint64_t value = 0u;
    for (size_t i = 0u; i < 8u; ++i) value = (value << 8) | bytes[i];
    return value;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 17u || size > VALUES_MAX_BYTES) return 0;
    uint64_t first = read_u64(data + 1u);
    uint64_t second = read_u64(data + 9u);
    size_t length = size - 17u;
    char *text = malloc(length + 1u);
    if (!text) return 0;
    memcpy(text, data + 17u, length);
    text[length] = '\0';

    static const char *const choices[] = {"low", "high", "", "low-", NULL};
    uint64_t u64 = 0u;
    uint32_t u32 = 0u;
    int64_t i64 = 0;
    int boolean = 0;
    size_t index = 0u;
    switch (data[0] % 8u) {
    case 0:
        if (maelys_cli_parse_u64_decimal(text, first, second, &u64) == 0 &&
            (u64 < first || u64 > second)) abort();
        break;
    case 1: {
        uint32_t low = (uint32_t)first;
        uint32_t high = (uint32_t)second;
        if (maelys_cli_parse_u32_decimal(text, low, high, &u32) == 0 &&
            (u32 < low || u32 > high)) abort();
        break;
    }
    case 2: {
        int64_t low = (int64_t)first;
        int64_t high = (int64_t)second;
        if (maelys_cli_parse_i64_decimal(text, low, high, &i64) == 0 &&
            (i64 < low || i64 > high)) abort();
        break;
    }
    case 3:
        if (maelys_cli_parse_byte_size(text, first, second, &u64) == 0 &&
            (u64 < first || u64 > second)) abort();
        break;
    case 4:
        if (maelys_cli_parse_duration_ms(text, first, second, &u64) == 0 &&
            (u64 < first || u64 > second)) abort();
        break;
    case 5:
        if (maelys_cli_parse_boolean(text, &boolean) == 0 &&
            boolean != 0 && boolean != 1) abort();
        break;
    case 6:
        if (maelys_cli_parse_choice(text, choices, &index) == 0 &&
            (index >= 4u || strcmp(choices[index], text) != 0)) abort();
        break;
    default:
        /* A hex value of the declared width is exactly that many lowercase
         * digits; the width comes from the input, bounded to what a
         * descriptor declares in practice. */
        if (maelys_cli_parse_hex(text, (size_t)(first % 129u)) == 0) {
            if (strlen(text) != (size_t)(first % 129u)) abort();
            for (const char *c = text; *c; ++c)
                if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'))) abort();
        }
        break;
    }
    free(text);
    return 0;
}
