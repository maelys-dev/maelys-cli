/* src/language.c: which language a line asks for, and which one a program
 * has. The tags are RFC 5646's, the matching RFC 4647's Lookup, the locale
 * conversion the contract's own rule.
 *
 * tests/languages.tsv is the corpus both this test and
 * python/tests/test_maelys_cli.py answer, so that C and Python resolve a
 * request alike. The corpus path is argv[1], tests/languages.tsv from the
 * repository root otherwise. What only C has -- a NULL, a buffer too
 * small -- is checked here. */
#include "check.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define LINE_MAX_BYTES 1024u
#define FIELDS 9u

static const char *corpus_path = "tests/languages.tsv";

/* `-` is an absent value, "" an empty one. */
static const char *value(const char *field) {
    if (!strcmp(field, "-")) return NULL;
    if (!strcmp(field, "\"\"")) return "";
    return field;
}

/* AVAILABLE: comma-separated tags, `-` for none. Splits `field` in place. */
static size_t available_list(char *field, const char **out, size_t size) {
    if (!strcmp(field, "-")) return 0u;
    size_t count = 0u;
    for (char *tag = strtok(field, ","); tag && count < size; tag = strtok(NULL, ","))
        out[count++] = tag;
    return count;
}

static int same_result(const char *got, const char *wanted) {
    return wanted ? got && !strcmp(got, wanted) : got == NULL;
}

static int test_corpus(void) {
    FILE *file = fopen(corpus_path, "r");
    if (!file) {
        (void)fprintf(stderr, "cannot open %s\n", corpus_path);
        return 0;
    }
    char line[LINE_MAX_BYTES];
    int number = 0, failures = 0, cases = 0;
    while (fgets(line, sizeof(line), file)) {
        ++number;
        line[strcspn(line, "\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *fields[FIELDS];
        size_t count = 0u;
        for (char *at = line; count < FIELDS; ) {
            fields[count++] = at;
            char *tab = strchr(at, '\t');
            if (!tab) break;
            *tab = '\0';
            at = tab + 1;
        }
        ++cases;
        const char *kind = fields[0];
        const char *available[MAELYS_CLI_MAX_LANGUAGES];
        if (!strcmp(kind, "tag") && count == 3u) {
            int wanted = !strcmp(fields[2], "yes");
            if (maelys_cli_language_tag_is_well_formed(value(fields[1])) != wanted) {
                (void)fprintf(stderr, "%s:%d: tag %s: wanted %s\n", corpus_path, number, fields[1], fields[2]);
                ++failures;
            }
        } else if (!strcmp(kind, "locale") && count == 3u) {
            char tag[64];
            int got = maelys_cli_language_from_locale(value(fields[1]), tag, sizeof(tag));
            int ok = !strcmp(fields[2], "-") ? got == -1 :
                     !strcmp(fields[2], "C") ? got == 0 && !tag[0] :
                     got == 1 && !strcmp(tag, fields[2]);
            if (!ok) {
                (void)fprintf(stderr, "%s:%d: locale %s gave %d '%s', wanted %s\n",
                              corpus_path, number, fields[1], got, tag, fields[2]);
                ++failures;
            }
        } else if (!strcmp(kind, "lookup") && count == 4u) {
            size_t n = available_list(fields[2], available, MAELYS_CLI_MAX_LANGUAGES);
            const char *got = maelys_cli_language_lookup(value(fields[1]), n ? available : NULL, n);
            if (!same_result(got, value(fields[3]))) {
                (void)fprintf(stderr, "%s:%d: lookup %s gave %s, wanted %s\n",
                              corpus_path, number, fields[1], got ? got : "-", fields[3]);
                ++failures;
            }
        } else if (!strcmp(kind, "resolve") && count == 8u) {
            size_t n = available_list(fields[6], available, MAELYS_CLI_MAX_LANGUAGES);
            const char *got = maelys_cli_language_resolve(value(fields[1]), value(fields[2]),
                value(fields[3]), value(fields[4]), value(fields[5]), n ? available : NULL, n);
            if (!same_result(got, value(fields[7]))) {
                (void)fprintf(stderr, "%s:%d: resolve gave %s, wanted %s\n",
                              corpus_path, number, got ? got : "-", fields[7]);
                ++failures;
            }
        } else if (!strcmp(kind, "conversions") && count == 3u) {
            char got[256];
            int status = maelys_cli_template_conversions(fields[1], got, sizeof(got));
            const char *wanted = strcmp(fields[2], "-") ? fields[2] : "";
            if (status != 0 || strcmp(got, wanted)) {
                (void)fprintf(stderr, "%s:%d: conversions of '%s' gave '%s', wanted '%s'\n",
                              corpus_path, number, fields[1], got, wanted);
                ++failures;
            }
        } else {
            (void)fprintf(stderr, "%s:%d: malformed line\n", corpus_path, number);
            ++failures;
        }
    }
    (void)fclose(file);
    CHECK(cases > 100);
    return failures == 0;
}

static int test_c_edges(void) {
    char tag[64];
    CHECK(!maelys_cli_language_tag_is_well_formed(NULL));
    CHECK(maelys_cli_language_from_locale(NULL, tag, sizeof(tag)) == -1);
    CHECK(maelys_cli_language_from_locale("fr_FR", NULL, 8u) == -1);
    CHECK(maelys_cli_language_from_locale("fr_FR", tag, 0u) == -1);
    /* A buffer too small for the tag gives the canonical language. */
    char small[4];
    CHECK(maelys_cli_language_from_locale("fr_FR.UTF-8", small, sizeof(small)) == 0 && !small[0]);
    /* Lookup answers with the program's own string, compared by pointer. */
    static const char *const available[] = {"fr"};
    CHECK(maelys_cli_language_lookup("FR-ca", available, 1u) == available[0]);
    CHECK(maelys_cli_language_lookup(NULL, available, 1u) == NULL);
    /* A tag longer than any a request may hold is no language. */
    char longest[80];
    memset(longest, 'a', sizeof(longest) - 1u);
    longest[0] = 'f'; longest[1] = 'r'; longest[2] = '-';
    longest[sizeof(longest) - 1u] = '\0';
    CHECK(maelys_cli_language_lookup(longest, available, 1u) == NULL);
    /* A conversion list that does not fit is refused, not cut. */
    char out[4];
    CHECK(maelys_cli_template_conversions("%s %s", out, sizeof(out)) == -1);
    CHECK(maelys_cli_template_conversions("50%! off", tag, sizeof(tag)) == -1);
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1) corpus_path = argv[1];
    int failures = 0;
    RUN(test_corpus);
    RUN(test_c_edges);
    return failures ? 1 : 0;
}
