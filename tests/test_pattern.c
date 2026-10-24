/* src/pattern.c: the reader and matcher of argument.pattern.
 *
 * tests/patterns.tsv is the corpus both this test and
 * python/tests/test_maelys_cli.py answer: every `check` line is a pattern
 * the subset admits or refuses, every `match` line a value it matches or
 * not. On top of it, where the dialects agree -- a pattern the subset
 * admits, a value of printable ASCII -- the libc's own regcomp()/regexec()
 * must answer as this matcher does: the subset is what the two engines
 * read alike, and this is the check that it is. The corpus path is argv[1],
 * tests/patterns.tsv from the repository root otherwise. */
#include "check.h"
#include "internal.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_BYTES 2048u

static const char *corpus_path = "tests/patterns.tsv";

/* \n is a newline and \\ a backslash, in VALUE only. */
static void unescape(char *text) {
    char *out = text;
    for (const char *in = text; *in; ++in) {
        if (*in == '\\' && in[1] == 'n') { *out++ = '\n'; ++in; }
        else if (*in == '\\' && in[1] == '\\') { *out++ = '\\'; ++in; }
        else *out++ = *in;
    }
    *out = '\0';
}

static int printable_ascii(const char *text) {
    for (; *text; ++text)
        if ((unsigned char)*text < 0x20u || (unsigned char)*text > 0x7eu) return 0;
    return 1;
}

/* glibc reads an anchor inside a repeated group wrongly (^(ab|cd$){1,3}e$
 * matches abcdabe for it, for no other engine): the libc is asked only
 * where `^` and `$` stand at the ends of the pattern. */
static int anchors_at_ends(const char *pattern) {
    size_t length = strlen(pattern);
    for (size_t i = 0u; i < length; ++i)
        if ((pattern[i] == '^' && i != 0u) || (pattern[i] == '$' && i + 1u != length))
            return 0;
    return 1;
}

static int test_corpus(void) {
    FILE *file = fopen(corpus_path, "r");
    if (!file) {
        (void)fprintf(stderr, "cannot open %s\n", corpus_path);
        return 0;
    }
    char line[LINE_MAX_BYTES];
    int checks = 0, matches = 0, compared = 0, failures = 0, number = 0;
    while (fgets(line, sizeof(line), file)) {
        number++;
        size_t length = strlen(line);
        if (length && line[length - 1u] == '\n') line[--length] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *fields[4] = {NULL, NULL, NULL, NULL};
        size_t count = 0u;
        for (char *cursor = line; cursor && count < 4u; ) {
            fields[count++] = cursor;
            cursor = strchr(cursor, '\t');
            if (cursor) *cursor++ = '\0';
        }
        if (!strcmp(fields[0], "check") && count == 3u) {
            checks++;
            char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
            int ok = maelys_cli_pattern_check(fields[1], reason, sizeof(reason)) == 0;
            int expected = !strcmp(fields[2], "ok");
            if (ok != expected) {
                (void)fprintf(stderr, "%s:%d: check '%s' expected %s, got %s%s%s\n",
                    corpus_path, number, fields[1], fields[2], ok ? "ok" : "refused",
                    ok ? "" : ": ", ok ? "" : reason);
                failures++;
            }
            if (!ok && !reason[0]) {
                (void)fprintf(stderr, "%s:%d: refused without a reason\n", corpus_path, number);
                failures++;
            }
            /* match answers -1 exactly where check refuses. */
            int matched = maelys_cli_pattern_match(fields[1], "x");
            if ((matched < 0) == ok) {
                (void)fprintf(stderr, "%s:%d: check and match disagree on '%s'\n",
                    corpus_path, number, fields[1]);
                failures++;
            }
            continue;
        }
        if (!strcmp(fields[0], "match") && count == 4u) {
            matches++;
            unescape(fields[2]);
            int expected = !strcmp(fields[3], "yes");
            int matched = maelys_cli_pattern_match(fields[1], fields[2]);
            if (matched != expected) {
                (void)fprintf(stderr, "%s:%d: match '%s' on '%s' expected %s, got %d\n",
                    corpus_path, number, fields[1], fields[2], fields[3], matched);
                failures++;
            }
            if (printable_ascii(fields[1]) && printable_ascii(fields[2]) &&
                anchors_at_ends(fields[1])) {
                regex_t compiled;
                if (regcomp(&compiled, fields[1], REG_EXTENDED | REG_NOSUB) == 0) {
                    int libc = regexec(&compiled, fields[2], 0u, NULL, 0) == 0;
                    regfree(&compiled);
                    compared++;
                    if (libc != expected) {
                        (void)fprintf(stderr, "%s:%d: the libc reads '%s' on '%s' as %s\n",
                            corpus_path, number, fields[1], fields[2], libc ? "yes" : "no");
                        failures++;
                    }
                }
            }
            continue;
        }
        (void)fprintf(stderr, "%s:%d: malformed line\n", corpus_path, number);
        failures++;
    }
    (void)fclose(file);
    (void)fprintf(stderr, "corpus: %d checks, %d matches, %d compared with the libc\n",
        checks, matches, compared);
    CHECK(checks >= 60 && matches >= 80 && compared >= 50);
    CHECK(failures == 0);
    return 1;
}

/* What a text file cannot carry: control characters, bytes that are not
 * UTF-8, and the corners of the two entry points. */
static int test_bytes(void) {
    char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
    CHECK(maelys_cli_pattern_check("^a\tb$", reason, sizeof(reason)) != 0);
    CHECK(strstr(reason, "control character"));
    CHECK(maelys_cli_pattern_check("^a\x7f$", reason, sizeof(reason)) != 0);
    CHECK(maelys_cli_pattern_check("^a\xc2\x85$", reason, sizeof(reason)) != 0);
    CHECK(maelys_cli_pattern_check("^\xff$", reason, sizeof(reason)) != 0);
    CHECK(strstr(reason, "not UTF-8"));
    CHECK(maelys_cli_pattern_check("^\xc3$", reason, sizeof(reason)) != 0);
    CHECK(maelys_cli_pattern_check("^\xed\xa0\x80$", reason, sizeof(reason)) != 0);
    CHECK(maelys_cli_pattern_check(NULL, reason, sizeof(reason)) != 0);
    CHECK(strstr(reason, "empty"));
    CHECK(maelys_cli_pattern_check("^a$", NULL, 0u) == 0);
    CHECK(maelys_cli_pattern_check("^(a$", NULL, 0u) != 0);
    /* A short reason buffer is cut, never overrun. */
    char small[8];
    CHECK(maelys_cli_pattern_check("^(a$", small, sizeof(small)) != 0);
    CHECK(strlen(small) == 7u);
    /* A byte that is not UTF-8 in the value is one character that no
     * literal and no bracket expression names, as Python reads argv. */
    CHECK(maelys_cli_pattern_match("^.$", "\xff") == 1);
    CHECK(maelys_cli_pattern_match("^..$", "\xff\xfe") == 1);
    CHECK(maelys_cli_pattern_match("^.$", "\xc3\xa9") == 1);
    CHECK(maelys_cli_pattern_match("^..$", "\xc3\xa9") == 0);
    CHECK(maelys_cli_pattern_match("^[^a]$", "\xff") == 1);
    CHECK(maelys_cli_pattern_match("^[a-z]$", "\xff") == 0);
    CHECK(maelys_cli_pattern_match("^a$", "\xff") == 0);
    CHECK(maelys_cli_pattern_match("^\xc3\xa9$", "\xc3\xa9") == 1);
    CHECK(maelys_cli_pattern_match("^\xc3\xa9$", "e") == 0);
    CHECK(maelys_cli_pattern_match("^a$", NULL) == 0);
    CHECK(maelys_cli_pattern_match(NULL, "a") == -1);
    CHECK(maelys_cli_pattern_match("", "a") == -1);
    /* A chain of splits with almost nothing to consume: each optional
     * group adds one, and the walk over them must stay inside its stack. */
    char chain[4096];
    size_t at = 0u;
    for (int i = 0; i < 30; ++i) chain[at++] = '(';
    chain[at++] = 'a';
    for (int i = 0; i < 30; ++i) { chain[at++] = ')'; chain[at++] = '?'; }
    chain[at] = '\0';
    CHECK(maelys_cli_pattern_match(chain, "") == 1);
    CHECK(maelys_cli_pattern_match(chain, "b") == 1);
    at = 0u;
    for (int i = 0; i < 1000; ++i) { memcpy(chain + at, "(a)?", 4u); at += 4u; }
    chain[at] = '\0';
    CHECK(maelys_cli_pattern_match(chain, "") == 1);
    CHECK(maelys_cli_pattern_match(chain, "aaaa") == 1);
    /* The largest program admitted runs, in bounded time and memory. */
    char many[300];
    memset(many, 'a', sizeof(many) - 1u);
    many[sizeof(many) - 1u] = '\0';
    CHECK(maelys_cli_pattern_match("^(a{255}){255}$", many) == 0);
    CHECK(maelys_cli_pattern_match("^(a{0,255}){0,128}$", many) == 1);
    CHECK(maelys_cli_pattern_match("^(a{0,255}){0,129}$", many) == -1);
    CHECK(maelys_cli_pattern_match("^(a?){255}$", many) == 0);
    many[255] = '\0';
    CHECK(maelys_cli_pattern_match("^(a?){255}$", many) == 1);
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1) corpus_path = argv[1];
    int failures = 0;
    RUN(test_corpus);
    RUN(test_bytes);
    return failures ? 1 : 0;
}
