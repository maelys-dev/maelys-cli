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

#include <pthread.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_BYTES 2048u

static const char *corpus_path = "tests/patterns.tsv";

/* In VALUE only: \n and \r are U+000A and U+000D, \uXXXX the code point
 * XXXX written in UTF-8, \\ a backslash. Never longer than the text. */
static void unescape(char *text) {
    char *out = text;
    for (const char *in = text; *in; ++in) {
        if (*in == '\\' && in[1] == 'n') { *out++ = '\n'; ++in; }
        else if (*in == '\\' && in[1] == 'r') { *out++ = '\r'; ++in; }
        else if (*in == '\\' && in[1] == '\\') { *out++ = '\\'; ++in; }
        else if (*in == '\\' && in[1] == 'u') {
            char hex[5] = {in[2], in[3], in[4], in[5], '\0'};
            unsigned long cp = strtoul(hex, NULL, 16);
            if (cp < 0x80u) *out++ = (char)cp;
            else if (cp < 0x800u) {
                *out++ = (char)(0xc0u | (cp >> 6));
                *out++ = (char)(0x80u | (cp & 0x3fu));
            } else {
                *out++ = (char)(0xe0u | (cp >> 12));
                *out++ = (char)(0x80u | ((cp >> 6) & 0x3fu));
                *out++ = (char)(0x80u | (cp & 0x3fu));
            }
            in += 5;
        } else *out++ = *in;
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
    CHECK(strstr(reason, "outside printable ASCII"));
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
    /* A pattern is printable ASCII: a literal é is outside the subset. */
    CHECK(maelys_cli_pattern_match("^\xc3\xa9$", "\xc3\xa9") == -1);
    CHECK(maelys_cli_pattern_check("^a\x7f$", reason, sizeof(reason)) != 0);
    CHECK(strstr(reason, "control character"));
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

/* The work is O((length + 1) x states), counted by the matcher itself
 * rather than timed: at most (length + 1) x (4 x states + 1) steps, on a
 * pattern backtracking engines take exponential time on, (a+)+b against
 * a...ac, and on the most expensive shapes the subset admits. */
static int test_linear_work(void) {
    static const char *const patterns[] = {
        "^(a+)+b$", "^(a|a)*b$", "^(a*)*(a?){200}b$", "(a?){255}a{255}c",
        "^[A-Za-z0-9_][A-Za-z0-9_.-]{0,254}$", "x(a|b|ab|ba)*y",
    };
    char *value = malloc(4097u);
    if (!value) return 0;
    for (size_t i = 0u; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        for (size_t length = 256u; length <= 4096u; length *= 2u) {
            memset(value, 'a', length);
            value[length] = '\0';
            uint64_t steps = 0u;
            uint32_t states = 0u;
            int matched = maelys_cli_pattern_match_counted(patterns[i], value, &steps, &states);
            CHECK(matched == 0 || matched == 1);
            CHECK(steps > 0u && states > 0u);
            CHECK(steps <= (uint64_t)(length + 1u) * (4u * (uint64_t)states + 1u));
        }
    }
    free(value);
    return 1;
}

/* The depth of every recursion is the nesting of groups, never the length:
 * a pattern of two million bytes is refused by the state bound without a
 * crash, a sequence of 30000 items and 20000 alternatives are read, and
 * the deepest nesting admitted runs on a thread with a small stack. */
static void *deep_on_small_stack(void *argument) {
    int *ok = argument;
    char pattern[200];
    size_t at = 0u;
    for (int i = 0; i < 32; ++i) pattern[at++] = '(';
    pattern[at++] = 'a';
    for (int i = 0; i < 32; ++i) { pattern[at++] = ')'; pattern[at++] = '*'; }
    pattern[at] = '\0';
    *ok = maelys_cli_pattern_check(pattern, NULL, 0u) == 0 &&
        maelys_cli_pattern_match(pattern, "aaaa") == 1;
    return NULL;
}

static int test_depth(void) {
    size_t length = 2000000u;
    char *pattern = malloc(length + 1u);
    if (!pattern) return 0;
    char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
    memset(pattern, 'a', length);
    pattern[length] = '\0';
    CHECK(maelys_cli_pattern_check(pattern, reason, sizeof(reason)) != 0);
    CHECK(strstr(reason, "65536 states"));
    /* 30000 items in a sequence; 20000 alternatives, under the state bound. */
    memset(pattern, 'a', 30000u);
    pattern[30000] = '\0';
    CHECK(maelys_cli_pattern_check(pattern, NULL, 0u) == 0);
    for (size_t i = 0u; i < 20000u; ++i) { pattern[2u * i] = 'a'; pattern[2u * i + 1u] = '|'; }
    pattern[39999] = '\0';
    CHECK(maelys_cli_pattern_match(pattern, "zza") == 1);
    free(pattern);
    pthread_attr_t attributes;
    pthread_t thread;
    int ok = 0;
    CHECK(pthread_attr_init(&attributes) == 0);
    CHECK(pthread_attr_setstacksize(&attributes, 256u * 1024u) == 0);
    CHECK(pthread_create(&thread, &attributes, deep_on_small_stack, &ok) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    (void)pthread_attr_destroy(&attributes);
    CHECK(ok);
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1) corpus_path = argv[1];
    int failures = 0;
    RUN(test_corpus);
    RUN(test_bytes);
    RUN(test_linear_work);
    RUN(test_depth);
    return failures ? 1 : 0;
}
