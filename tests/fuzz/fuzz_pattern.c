/* SPDX-License-Identifier: MPL-2.0 */
/* src/pattern.c on arbitrary bytes: the reader of the common subset of
 * ECMA-262 and POSIX ERE and the matcher behind it, and the properties a
 * caller relies on: check refuses exactly the patterns match answers -1
 * to; a refused pattern comes with a reason; and, on a pattern the subset
 * admits and a value of printable ASCII, the libc's regexec() answers as
 * the matcher does, since the subset is what the two engines read alike.
 * That comparison is made on a short pattern with one small bound at most:
 * the libc expands a bound into its copies, and nested ones -- (a{255}){255},
 * which this matcher runs in kilobytes -- cost glibc's regcomp gigabytes,
 * the very cost src/pattern.c exists to avoid; even one {255} on a long
 * group costs it tens of megabytes per input, which the allocator of the
 * sanitizers keeps until the campaign runs out of memory, and a group that
 * can match nothing, repeated, costs it time exponential in the bound
 * (7(a?|c*){20,}: 0.8 s for glibc, 0.01 ms here). And it is made
 * only when `^` and `$` stand at the ends of the pattern: glibc reads an
 * anchor inside a repeated group wrongly -- ^(ab|cd$){1,3}e$ matches
 * abcdabe for it, for no other engine -- and tests/patterns.tsv holds the
 * right answer.
 *
 * Input: the pattern, a NUL, then the value (NUL-terminated here, as argv
 * words are). */
#include "internal.h"
#include "replay.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define PATTERN_MAX_BYTES 4096u
#define LIBC_MAX_BYTES 128u
#define LIBC_MAX_BOUND 8ul

static int printable_ascii(const char *text) {
    for (; *text; ++text)
        if ((unsigned char)*text < 0x20u || (unsigned char)*text > 0x7eu) return 0;
    return 1;
}

/* What the libc can compile cheaply and read right: short, one bound at
 * most, small, and anchors only at the ends. */
static int libc_affordable(const char *pattern) {
    size_t length = strlen(pattern);
    if (length > LIBC_MAX_BYTES) return 0;
    for (size_t i = 0u; i < length; ++i)
        if ((pattern[i] == '^' && i != 0u) || (pattern[i] == '$' && i + 1u != length))
            return 0;
    const char *bound = strchr(pattern, '{');
    if (!bound) return 1;
    if (strchr(bound + 1, '{')) return 0;
    for (const char *p = bound + 1; *p && *p != '}'; ) {
        if (*p >= '0' && *p <= '9') {
            char *end = NULL;
            if (strtoul(p, &end, 10) > LIBC_MAX_BOUND) return 0;
            p = end;
        } else {
            ++p;
        }
    }
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
    if (admitted && printable_ascii(pattern) && printable_ascii(value) &&
        libc_affordable(pattern)) {
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
