/* SPDX-License-Identifier: MPL-2.0 */
/* The language of human text: which one a line asks for, and which one a
 * program has.
 *
 * A request is a language tag of the form agent-cli/v2 section 5 writes,
 * language[-Script][-REGION], the subset of RFC 5646 a catalog declares as
 * the pattern of --lang, compared without regard to case: fr-CA and FR-ca
 * are one preference.
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

static int equal_ignoring_case(const char *a, const char *b, size_t n) {
    for (size_t i = 0u; i < n; ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

/* language[-Script][-REGION]: two or three letters, then a script of four
 * letters and a region of two letters or three digits, each optional and
 * after a '-' (MAELYS_CLI_LANGUAGE_TAG_PATTERN). */
int maelys_cli_language_tag_is_well_formed(const char *tag) {
    if (!tag) return 0;
    size_t n = 0u;
    while (is_alpha((unsigned char)tag[n])) n++;
    if (n < 2u || n > 3u) return 0;
    const char *p = tag + n;
    if (p[0] == '-' && all(p + 1, 4u, is_alpha) && strlen(p + 1) >= 4u &&
        (p[5] == '\0' || p[5] == '-'))
        p += 5;
    if (p[0] == '-') {
        size_t m = strlen(p + 1);
        if (!((m == 2u && all(p + 1, 2u, is_alpha)) || (m == 3u && all(p + 1, 3u, is_digit))))
            return 0;
        p += 1u + m;
    }
    return p[0] == '\0';
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

/* ---- translations ------------------------------------------------------ */

const char *maelys_cli_translate(const maelys_cli_language_t *product,
                                 const maelys_cli_language_t *framework,
                                 const char *source) {
    if (!source) return source;
    const maelys_cli_language_t *tables[2] = {product, framework};
    for (size_t t = 0u; t < 2u; ++t) {
        if (!tables[t] || !tables[t]->translations) continue;
        for (size_t i = 0u; i < tables[t]->translation_count; ++i) {
            const maelys_cli_translation_t *entry = &tables[t]->translations[i];
            if (entry->source && entry->text && !strcmp(entry->source, source))
                return entry->text;
        }
    }
    return source;
}

/* The length of the conversion at `p` (a '%' that is not "%%"): flags,
 * width, precision, length modifier and letter; 0 when it is not one. */
static size_t conversion_length(const char *p) {
    size_t n = 1u;
    while (p[n] && strchr("-+ #0", p[n])) n++;
    if (p[n] == '*') n++; else while (is_digit((unsigned char)p[n])) n++;
    if (p[n] == '.') {
        n++;
        if (p[n] == '*') n++; else while (is_digit((unsigned char)p[n])) n++;
    }
    while (p[n] && strchr("hljztL", p[n])) n++;
    if (!p[n] || !strchr("diuoxXcspfFeEgGaA", p[n])) return 0u;
    return n + 1u;
}

int maelys_cli_template_conversions(const char *format, char *out, size_t size) {
    if (!format || !out || size == 0u) return -1;
    size_t used = 0u;
    out[0] = '\0';
    for (const char *p = format; *p; ++p) {
        if (*p != '%') continue;
        if (p[1] == '%') { ++p; continue; }
        size_t n = conversion_length(p);
        if (n == 0u || used + n + 2u > size) return -1;
        if (used) out[used++] = ' ';
        memcpy(out + used, p, n);
        used += n;
        out[used] = '\0';
        p += n - 1u;
    }
    return 0;
}

int maelys_cli_translate_error(const maelys_cli_language_t *product,
                               const maelys_cli_language_t *framework,
                               const maelys_cli_error_t *error, char *out, size_t size) {
    if (!error || !out || size == 0u) return 0;
    const char *template = error->format ?
        maelys_cli_translate(product, framework, error->format) : NULL;
    if (!template || template == error->format) {
        /* A message assembled before it reached the framework is translated
         * only when it is, word for word, a text a table names. */
        const char *whole = maelys_cli_translate(product, framework, error->message);
        if (whole == error->message) return 0;
        (void)snprintf(out, size, "%s", whole);
        return 1;
    }
    const char *argument = error->arguments;
    unsigned taken = 0u;
    size_t used = 0u;
    for (const char *p = template; *p && used + 1u < size; ++p) {
        if (*p != '%') { out[used++] = *p; continue; }
        if (p[1] == '%') { out[used++] = '%'; ++p; continue; }
        size_t n = conversion_length(p);
        if (n == 0u || taken >= error->argument_count) return 0;
        size_t length = strlen(argument);
        if (used + length >= size) length = size - used - 1u;
        memcpy(out + used, argument, length);
        used += length;
        argument += strlen(argument) + 1u;
        taken++;
        p += n - 1u;
    }
    out[used] = '\0';
    return taken == error->argument_count;
}

extern const maelys_cli_language_t maelys_cli_framework_fr;

const maelys_cli_language_t *maelys_cli_framework_language(const char *tag) {
    static const char *const tags[] = {"fr"};
    const char *found = maelys_cli_language_lookup(tag, tags, 1u);
    return found ? &maelys_cli_framework_fr : NULL;
}
