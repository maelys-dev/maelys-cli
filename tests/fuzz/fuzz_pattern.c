/* SPDX-License-Identifier: MPL-2.0 */
/* maelys/cli/regex.h on arbitrary bytes: the reader of the subset of
 * agent-cli/v2 section 3 and the matcher behind it, and the properties a
 * caller relies on: a refusal comes with its family, a reason and an offset
 * inside the pattern, and no handle; an admitted pattern searches any value,
 * NUL bytes and bytes of no UTF-8 sequence included, and says 0 or 1; the
 * internal reader of argument.pattern agrees with the public API.
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
 * Input: the pattern, a NUL, then the value, every byte of it, NUL bytes
 * included. The whole input is also read as a pattern, NUL bytes and all. */
#include "internal.h"
#include "maelys/cli/regex.h"
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
    size_t pattern_length = strlen(pattern);
    const char *value = bytes + pattern_length + 1u;
    size_t value_length = pattern_length < size ? size - pattern_length - 1u : 0u;

    maelys_cli_regex_t *regex = NULL;
    maelys_cli_regex_diagnostic_t diagnostic;
    maelys_cli_regex_status_t status = maelys_cli_regex_compile(
        pattern, pattern_length, &regex, &diagnostic);
    int admitted = status == MAELYS_CLI_REGEX_OK;
    if (admitted != (regex != NULL) || diagnostic.status != status) abort();
    if (!admitted && (!diagnostic.message || diagnostic.offset > pattern_length ||
            status < MAELYS_CLI_REGEX_EMPTY || status > MAELYS_CLI_REGEX_LIMIT))
        abort();
    if (admitted) {
        int matched = maelys_cli_regex_search(regex, value, value_length);
        if (matched != 0 && matched != 1) abort();
        if (maelys_cli_regex_states(regex) == 0u ||
            maelys_cli_regex_states(regex) > MAELYS_CLI_REGEX_MAX_STATES)
            abort();
        /* The reader of argument.pattern says what the public API says,
         * on the part of the value a C string holds. */
        size_t prefix = strlen(value);
        if (maelys_cli_pattern_match(pattern, value) !=
            maelys_cli_regex_search(regex, value, prefix))
            abort();
#ifdef MAELYS_CLI_FUZZ_RE2
        if (prefix == value_length && valid_utf8((const unsigned char *)value)) {
            int reference = maelys_cli_fuzz_re2_match(pattern, value);
            if (reference == -1 || (reference >= 0 && reference != matched)) abort();
        }
#endif
    }
    maelys_cli_regex_free(regex);
    char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
    if ((maelys_cli_pattern_check(pattern, reason, sizeof(reason)) == 0) != admitted) abort();
    /* The whole input as one pattern: a NUL byte is a control character. */
    regex = NULL;
    status = maelys_cli_regex_compile((const char *)data, size, &regex, &diagnostic);
    if (status == MAELYS_CLI_REGEX_OK && memchr(data, '\0', size)) abort();
    if (status != MAELYS_CLI_REGEX_OK && (regex || diagnostic.offset > size)) abort();
    maelys_cli_regex_free(regex);
    free(bytes);
    return 0;
}
