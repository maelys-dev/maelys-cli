/* SPDX-License-Identifier: MPL-2.0 */
/* src/pattern.c on arbitrary bytes: the reader of the common subset of
 * ECMA-262 and POSIX ERE and the matcher behind it, and the properties a
 * caller relies on: check refuses exactly the patterns match answers -1
 * to; a refused pattern comes with a reason; and, on a pattern the subset
 * admits and a value of printable ASCII, the libc's regexec() answers as
 * the matcher does, since the subset is what the two engines read alike.
 *
 * Input: the pattern, a NUL, then the value (NUL-terminated here, as argv
 * words are). */
#include "internal.h"
#include "replay.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define PATTERN_MAX_BYTES 4096u

static int printable_ascii(const char *text) {
    for (; *text; ++text)
        if ((unsigned char)*text < 0x20u || (unsigned char)*text > 0x7eu) return 0;
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > PATTERN_MAX_BYTES) return 0;
    char *bytes = malloc(size + 2u);
    if (!bytes) return 0;
    memcpy(bytes, data, size);
    bytes[size] = '\0';
    bytes[size + 1u] = '\0';
    const char *pattern = bytes;
    const char *value = bytes + strlen(pattern) + 1u;

    char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
    int admitted = maelys_cli_pattern_check(pattern, reason, sizeof(reason)) == 0;
    if (!admitted && !reason[0]) abort();
    int matched = maelys_cli_pattern_match(pattern, value);
    if (admitted != (matched >= 0)) abort();
    if (admitted && printable_ascii(pattern) && printable_ascii(value)) {
        regex_t compiled;
        if (regcomp(&compiled, pattern, REG_EXTENDED | REG_NOSUB) == 0) {
            int libc = regexec(&compiled, value, 0u, NULL, 0) == 0;
            regfree(&compiled);
            if (libc != matched) abort();
        }
    }
    free(bytes);
    return 0;
}
