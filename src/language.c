/* SPDX-License-Identifier: MPL-2.0 */
/* The language of human text: which one a line asks for, and which one a
 * program has.
 *
 * A request is a language tag, well formed as RFC 5646 section 2.1 writes
 * it, compared without regard to case: fr-CA and FR-ca are one preference.
 * A locale (LC_ALL, LC_MESSAGES, LANG) is converted by its own rule,
 * language[_territory][.codeset][@modifier] giving language-territory; C,
 * POSIX and C.* give the canonical language. The available language a
 * request resolves to is the Lookup of RFC 4647 section 3.4: the tag is
 * truncated from the right until a language the program has is found, the
 * canonical one in the end. Nothing here reads the locales installed on the
 * machine: the syntax is the RFC's, the same everywhere. */
#include "internal.h"

#include <ctype.h>
#include <string.h>

static int is_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int is_alnum(unsigned char c) { return is_alpha(c) || is_digit(c); }

static int all(const char *s, size_t n, int (*test)(unsigned char)) {
    for (size_t i = 0u; i < n; ++i)
        if (!test((unsigned char)s[i])) return 0;
    return 1;
}

/* The tags RFC 5646 keeps from RFC 3066 that its langtag production does
 * not cover, compared without regard to case. */
static const char *const grandfathered[] = {
    "en-GB-oed", "i-ami", "i-bnn", "i-default", "i-enochian", "i-hak",
    "i-klingon", "i-lux", "i-mingo", "i-navajo", "i-pwn", "i-tao", "i-tay",
    "i-tsu", "sgn-BE-FR", "sgn-BE-NL", "sgn-CH-DE", "art-lojban",
    "cel-gaulish", "no-bok", "no-nyn", "zh-guoyu", "zh-hakka", "zh-min",
    "zh-min-nan", "zh-xiang",
};

static int equal_ignoring_case(const char *a, const char *b, size_t n) {
    for (size_t i = 0u; i < n; ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

/* privateuse = "x" 1*("-" (1*8alphanum)), from subtag `at` of `count`. */
static int privateuse_from(const char *const *subtags, const size_t *lengths,
                           size_t at, size_t count) {
    if (at >= count || lengths[at] != 1u || tolower((unsigned char)subtags[at][0]) != 'x')
        return 0;
    if (at + 1u >= count) return 0;
    for (size_t i = at + 1u; i < count; ++i)
        if (lengths[i] < 1u || lengths[i] > 8u) return 0;
    return 1;
}

int maelys_cli_language_tag_is_well_formed(const char *tag) {
    if (!tag || !*tag) return 0;
    size_t length = strlen(tag);
    for (size_t i = 0u; i < sizeof(grandfathered) / sizeof(grandfathered[0]); ++i)
        if (strlen(grandfathered[i]) == length && equal_ignoring_case(tag, grandfathered[i], length))
            return 1;
    /* Subtags of 1 to 8 letters and digits, separated by one '-'. */
    const char *subtags[64];
    size_t lengths[64];
    size_t count = 0u;
    for (const char *p = tag; ; ) {
        const char *end = strchr(p, '-');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n < 1u || n > 8u || !all(p, n, is_alnum) || count == 64u) return 0;
        subtags[count] = p;
        lengths[count++] = n;
        if (!end) break;
        p = end + 1;
    }
    size_t at = 0u;
    if (privateuse_from(subtags, lengths, 0u, count)) return 1;
    /* language = 2*3ALPHA ["-" extlang] / 4ALPHA / 5*8ALPHA */
    if (!all(subtags[0], lengths[0], is_alpha) || lengths[0] < 2u) return 0;
    at = 1u;
    if (lengths[0] <= 3u) {
        /* extlang = 3ALPHA *2("-" 3ALPHA) */
        for (int e = 0; e < 3 && at < count && lengths[at] == 3u &&
                all(subtags[at], 3u, is_alpha); ++e)
            at++;
    }
    /* ["-" script], script = 4ALPHA */
    if (at < count && lengths[at] == 4u && all(subtags[at], 4u, is_alpha)) at++;
    /* ["-" region], region = 2ALPHA / 3DIGIT */
    if (at < count && ((lengths[at] == 2u && all(subtags[at], 2u, is_alpha)) ||
                       (lengths[at] == 3u && all(subtags[at], 3u, is_digit))))
        at++;
    /* *("-" variant), variant = 5*8alphanum / (DIGIT 3alphanum) */
    while (at < count && ((lengths[at] >= 5u) ||
                          (lengths[at] == 4u && is_digit((unsigned char)subtags[at][0]))))
        at++;
    /* *("-" extension), extension = singleton 1*("-" (2*8alphanum)) */
    while (at < count && lengths[at] == 1u && tolower((unsigned char)subtags[at][0]) != 'x') {
        at++;
        size_t parts = 0u;
        while (at < count && lengths[at] >= 2u) { at++; parts++; }
        if (parts == 0u) return 0;
    }
    /* ["-" privateuse] */
    if (at < count && privateuse_from(subtags, lengths, at, count)) return 1;
    return at == count;
}

int maelys_cli_language_from_locale(const char *locale, char *tag, size_t size) {
    if (!tag || size == 0u) return -1;
    tag[0] = '\0';
    if (!locale || !*locale) return -1;
    if (!strcmp(locale, "C") || !strcmp(locale, "POSIX") || !strncmp(locale, "C.", 2u))
        return 0;
    /* language[_territory][.codeset][@modifier] */
    size_t language = 0u;
    while (is_alpha((unsigned char)locale[language])) language++;
    if (language < 2u || language > 3u) return 0;
    size_t territory_start = language, territory = 0u;
    if (locale[language] == '_') {
        territory_start = language + 1u;
        while (is_alnum((unsigned char)locale[territory_start + territory])) territory++;
        if (territory != 2u && territory != 3u) return 0;
    }
    size_t rest = territory ? territory_start + territory : language;
    if (locale[rest] && locale[rest] != '.' && locale[rest] != '@') return 0;
    if (language + 1u + territory + 1u > size) return 0;
    size_t used = 0u;
    for (size_t i = 0u; i < language; ++i) tag[used++] = (char)tolower((unsigned char)locale[i]);
    if (territory) {
        tag[used++] = '-';
        for (size_t i = 0u; i < territory; ++i)
            tag[used++] = (char)toupper((unsigned char)locale[territory_start + i]);
    }
    tag[used] = '\0';
    return maelys_cli_language_tag_is_well_formed(tag) ? 1 : (tag[0] = '\0', 0);
}

const char *maelys_cli_language_lookup(const char *tag, const char *const *available,
                                       size_t count) {
    if (!tag || !*tag || !available) return NULL;
    char range[64];
    size_t length = strlen(tag);
    if (length >= sizeof(range)) return NULL;
    memcpy(range, tag, length + 1u);
    while (length > 0u) {
        for (size_t i = 0u; i < count; ++i)
            if (available[i] && strlen(available[i]) == length &&
                equal_ignoring_case(range, available[i], length))
                return available[i];
        /* Truncate the last subtag, and a singleton left last with it. */
        char *dash = strrchr(range, '-');
        if (!dash) break;
        *dash = '\0';
        length = (size_t)(dash - range);
        char *previous = strrchr(range, '-');
        const char *last = previous ? previous + 1 : range;
        if (strlen(last) == 1u) {
            if (!previous) break;
            *previous = '\0';
            length = (size_t)(previous - range);
        }
    }
    return NULL;
}

/* The request in force, from the first source that is not empty: the
 * option (strict), the implementation's variable (lenient), then LC_ALL,
 * LC_MESSAGES and LANG. A request that names no available language falls
 * back to the canonical one: a later source is never read. */
const char *maelys_cli_language_resolve(
    const char *option, const char *variable, const char *lc_all,
    const char *lc_messages, const char *lang,
    const char *const *available, size_t count) {
    char tag[64];
    if (option && *option)
        return maelys_cli_language_lookup(option, available, count);
    if (variable && *variable)
        return maelys_cli_language_tag_is_well_formed(variable) ?
            maelys_cli_language_lookup(variable, available, count) : NULL;
    const char *locales[] = {lc_all, lc_messages, lang};
    for (size_t i = 0u; i < 3u; ++i) {
        if (!locales[i] || !*locales[i]) continue;
        return maelys_cli_language_from_locale(locales[i], tag, sizeof(tag)) == 1 ?
            maelys_cli_language_lookup(tag, available, count) : NULL;
    }
    return NULL;
}
