/* SPDX-License-Identifier: MPL-2.0 */
/* src/pattern.c on arbitrary bytes: the reader of the common subset of
 * ECMA-262 and POSIX ERE and the matcher behind it, and the properties a
 * caller relies on: check refuses exactly the patterns match answers -1
 * to; a refused pattern comes with a reason.
 *
 * Under `make fuzz` the harness is linked with tests/fuzz/re2_oracle.cc
 * (MAELYS_CLI_FUZZ_RE2) and holds the matcher to RE2 as well: on every
 * pattern the subset admits and every value that is UTF-8, the two answer
 * alike, and RE2 refuses no admitted pattern as a syntax error. The libc is
 * asked nothing here: glibc's regcomp is no reliable witness of the subset
 * (gigabytes on nested bounds, exponential time on a repeated group that
 * can match nothing, a wrong reading of an anchor inside a repeated group),
 * which RE2 is by construction. `make fuzz-smoke` replays the corpus without
 * RE2, on every leg.
 *
 * Input: the pattern, a NUL, then the value (NUL-terminated here, as argv
 * words are). */
#include "internal.h"
#include "replay.h"

#include <stdlib.h>
#include <string.h>

#define PATTERN_MAX_BYTES 4096u

#ifdef MAELYS_CLI_FUZZ_RE2
/* tests/fuzz/re2_oracle.cc: 1 or 0, -1 for a syntax error, -2 for a
 * pattern beyond RE2's limits. */
int maelys_cli_fuzz_re2_match(const char *pattern, const char *value);

/* RE2 reads its text as UTF-8; a value that is not is left to the
 * matcher's own tests, which read such a byte as one character. */
static int valid_utf8(const unsigned char *s) {
    while (*s) {
        unsigned char b = *s;
        size_t need = b < 0x80u ? 0u : b >= 0xc2u && b <= 0xdfu ? 1u :
            b >= 0xe0u && b <= 0xefu ? 2u : b >= 0xf0u && b <= 0xf4u ? 3u : 4u;
        if (need == 4u) return 0;
        unsigned long cp = need == 0u ? b : need == 1u ? b & 0x1fu :
            need == 2u ? b & 0x0fu : b & 0x07u;
        for (size_t i = 1u; i <= need; ++i) {
            if ((s[i] & 0xc0u) != 0x80u) return 0;
            cp = (cp << 6) | (s[i] & 0x3fu);
        }
        if ((need == 2u && (cp < 0x800u || (cp >= 0xd800u && cp <= 0xdfffu))) ||
            (need == 3u && (cp < 0x10000u || cp > 0x10ffffu)))
            return 0;
        s += need + 1u;
    }
    return 1;
}
#endif

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
#ifdef MAELYS_CLI_FUZZ_RE2
    if (admitted && valid_utf8((const unsigned char *)value)) {
        int reference = maelys_cli_fuzz_re2_match(pattern, value);
        if (reference == -1 || (reference >= 0 && reference != matched)) abort();
    }
#endif
    free(bytes);
    return 0;
}
