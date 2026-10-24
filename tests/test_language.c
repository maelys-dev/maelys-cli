/* src/language.c: which language a line asks for, and which one a program
 * has. The tags are RFC 5646's, the matching RFC 4647's Lookup, the locale
 * conversion the contract's own rule. */
#include "check.h"
#include "internal.h"

#include <string.h>

static int test_well_formed(void) {
    static const char *const good[] = {
        "en", "fr", "FR", "fr-CA", "FR-ca", "de-DE", "zh-Hant", "zh-Hant-TW",
        "sr-Latn-RS", "es-419", "de-CH-1901", "sl-rozaj-biske", "hy-Latn-IT-arevela",
        "en-US-u-islamcal", "de-DE-u-co-phonebk", "en-a-bbb-x-a-ccc", "x-whatever",
        "qaa-Qaaa-QM-x-southern", "zh-yue-HK", "i-klingon", "I-KLINGON", "en-GB-oed",
        "art-lojban", "zh-min-nan", "abcd", "abcdefgh", "de-1996", "english-language",
    };
    for (size_t i = 0u; i < sizeof(good) / sizeof(good[0]); ++i)
        if (!maelys_cli_language_tag_is_well_formed(good[i])) {
            (void)fprintf(stderr, "refused: %s\n", good[i]);
            return 0;
        }
    static const char *const bad[] = {
        "", "e", "en-US-US", "fr_FR", "fr-", "-fr", "fr--CA", "fr-CA-",
        "123", "fr-CA-x", "en-a", "en-a-x-b", "fr.UTF-8", "fr CA", "abcdefghi",
        "abcd-efg", "en-US-x-toolongsubtag", "de-419-DE", "é", "fr-é",
    };
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (maelys_cli_language_tag_is_well_formed(bad[i])) {
            (void)fprintf(stderr, "admitted: %s\n", bad[i]);
            return 0;
        }
    CHECK(!maelys_cli_language_tag_is_well_formed(NULL));
    return 1;
}

static int test_locales(void) {
    char tag[64];
    CHECK(maelys_cli_language_from_locale("fr_FR.UTF-8", tag, sizeof(tag)) == 1 && !strcmp(tag, "fr-FR"));
    CHECK(maelys_cli_language_from_locale("fr_CA", tag, sizeof(tag)) == 1 && !strcmp(tag, "fr-CA"));
    CHECK(maelys_cli_language_from_locale("fr", tag, sizeof(tag)) == 1 && !strcmp(tag, "fr"));
    CHECK(maelys_cli_language_from_locale("FR_fr.utf8@euro", tag, sizeof(tag)) == 1 && !strcmp(tag, "fr-FR"));
    CHECK(maelys_cli_language_from_locale("en_US.UTF-8", tag, sizeof(tag)) == 1 && !strcmp(tag, "en-US"));
    CHECK(maelys_cli_language_from_locale("es_419.UTF-8", tag, sizeof(tag)) == 1 && !strcmp(tag, "es-419"));
    /* The canonical language: C, POSIX, C.*, and what does not convert. */
    CHECK(maelys_cli_language_from_locale("C", tag, sizeof(tag)) == 0 && !tag[0]);
    CHECK(maelys_cli_language_from_locale("POSIX", tag, sizeof(tag)) == 0);
    CHECK(maelys_cli_language_from_locale("C.UTF-8", tag, sizeof(tag)) == 0);
    CHECK(maelys_cli_language_from_locale("C.utf8", tag, sizeof(tag)) == 0);
    CHECK(maelys_cli_language_from_locale("french", tag, sizeof(tag)) == 0 && !tag[0]);
    CHECK(maelys_cli_language_from_locale("fr_FRANCE", tag, sizeof(tag)) == 0);
    CHECK(maelys_cli_language_from_locale("fr-FR", tag, sizeof(tag)) == 0);
    CHECK(maelys_cli_language_from_locale("", tag, sizeof(tag)) == -1);
    CHECK(maelys_cli_language_from_locale(NULL, tag, sizeof(tag)) == -1);
    char small[4];
    CHECK(maelys_cli_language_from_locale("fr_FR.UTF-8", small, sizeof(small)) == 0 && !small[0]);
    return 1;
}

static int test_lookup(void) {
    static const char *const available[] = {"fr"};
    static const char *const regional[] = {"fr", "fr-CA"};
    CHECK(maelys_cli_language_lookup("fr", available, 1u) == available[0]);
    CHECK(maelys_cli_language_lookup("FR", available, 1u) == available[0]);
    CHECK(maelys_cli_language_lookup("fr-CA", available, 1u) == available[0]);
    CHECK(maelys_cli_language_lookup("fr-Latn-CA-x-private", available, 1u) == available[0]);
    CHECK(maelys_cli_language_lookup("de-DE", available, 1u) == NULL);
    CHECK(maelys_cli_language_lookup("en", available, 1u) == NULL);
    CHECK(maelys_cli_language_lookup("x-fr", available, 1u) == NULL);
    CHECK(maelys_cli_language_lookup("fr-ca", regional, 2u) == regional[1]);
    CHECK(maelys_cli_language_lookup("fr-BE", regional, 2u) == regional[0]);
    /* A singleton left last is truncated with the subtag after it. */
    CHECK(maelys_cli_language_lookup("fr-a-bcd", available, 1u) == available[0]);
    CHECK(maelys_cli_language_lookup("fr", NULL, 0u) == NULL);
    return 1;
}

static int test_resolve(void) {
    static const char *const available[] = {"fr"};
    const char *const *a = available;
    /* The option decides when given; then the variable; then the locales. */
    CHECK(maelys_cli_language_resolve("fr", "en", "en_US", NULL, NULL, a, 1u) == available[0]);
    CHECK(maelys_cli_language_resolve("en", "fr", "fr_FR", NULL, NULL, a, 1u) == NULL);
    CHECK(maelys_cli_language_resolve(NULL, "fr-CA", "en_US", NULL, NULL, a, 1u) == available[0]);
    CHECK(maelys_cli_language_resolve(NULL, NULL, "fr_FR.UTF-8", "en_US", "en_US", a, 1u) == available[0]);
    CHECK(maelys_cli_language_resolve(NULL, NULL, NULL, "fr_FR", "en_US", a, 1u) == available[0]);
    CHECK(maelys_cli_language_resolve(NULL, NULL, NULL, NULL, "fr_FR.UTF-8", a, 1u) == available[0]);
    CHECK(maelys_cli_language_resolve(NULL, NULL, NULL, NULL, NULL, a, 1u) == NULL);
    /* An empty source is skipped; the first one that is not decides, even
     * for a language the program does not have or a value that is not one. */
    CHECK(maelys_cli_language_resolve("", "", "", "", "fr_FR", a, 1u) == available[0]);
    CHECK(maelys_cli_language_resolve(NULL, NULL, "de_DE", "fr_FR", "fr_FR", a, 1u) == NULL);
    CHECK(maelys_cli_language_resolve(NULL, NULL, "C", "fr_FR", "fr_FR", a, 1u) == NULL);
    CHECK(maelys_cli_language_resolve(NULL, NULL, "C.UTF-8", NULL, "fr_FR", a, 1u) == NULL);
    CHECK(maelys_cli_language_resolve(NULL, "not a tag", "fr_FR", NULL, NULL, a, 1u) == NULL);
    CHECK(maelys_cli_language_resolve(NULL, "de", "fr_FR", NULL, NULL, a, 1u) == NULL);
    /* A program with no language but its canonical one. */
    CHECK(maelys_cli_language_resolve("fr", NULL, NULL, NULL, NULL, NULL, 0u) == NULL);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_well_formed);
    RUN(test_locales);
    RUN(test_lookup);
    RUN(test_resolve);
    return failures ? 1 : 0;
}
