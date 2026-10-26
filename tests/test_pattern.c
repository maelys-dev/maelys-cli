/* src/pattern.c: the reader and matcher of argument.pattern, and the
 * public API it is (maelys/cli/regex.h).
 *
 * tests/patterns.tsv is the corpus both this test and
 * python/tests/test_maelys_cli.py answer: every `check` line is a pattern
 * the subset admits, or refuses with the family it names, every `match`
 * line a value it matches or not. On top of it, where the dialects agree -- a pattern the subset
 * admits, a value of printable ASCII -- the libc's own regcomp()/regexec()
 * must answer as this matcher does: the subset is what the two engines
 * read alike, and this is the check that it is. The corpus path is argv[1],
 * tests/patterns.tsv from the repository root otherwise. */
#include "check.h"
#include "internal.h"
#include "maelys/cli/regex.h"

#include <errno.h>
#include <pthread.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_BYTES 2048u

static const char *corpus_path = "tests/patterns.tsv";

/* Built with its own copy of src/pattern.c and MAELYS_CLI_ALLOC_HOOK, as
 * test_files is with files.c: the countdown fails the allocation it reaches
 * zero on, so that every NO_MEMORY and ENOMEM path runs. */
static int alloc_countdown = -1;
int maelys_cli_test_alloc(void);
int maelys_cli_test_alloc(void) {
    if (alloc_countdown < 0) return 0;
    return alloc_countdown-- == 0;
}

/* The generation a search of that copy starts from (see test_generation_wrap). */
extern uint32_t maelys_cli_test_generation;

static const char *const family_names[] = {
    "ok", "invalid-argument", "no-memory", "empty", "encoding", "escape",
    "bracket", "group", "quantifier", "bound", "limit",
};

/* In VALUE only: \n and \r are U+000A and U+000D, \uXXXX the code point
 * XXXX written in UTF-8, \xHH the byte HH, \\ a backslash. Never longer
 * than the text; returns the length, a NUL byte being part of it. */
static size_t unescape(char *text) {
    char *out = text;
    for (const char *in = text; *in; ++in) {
        if (*in == '\\' && in[1] == 'n') { *out++ = '\n'; ++in; }
        else if (*in == '\\' && in[1] == 'r') { *out++ = '\r'; ++in; }
        else if (*in == '\\' && in[1] == '\\') { *out++ = '\\'; ++in; }
        else if (*in == '\\' && in[1] == 'x') {
            char hex[3] = {in[2], in[3], '\0'};
            *out++ = (char)strtoul(hex, NULL, 16);
            in += 3;
        }
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
    return (size_t)(out - text);
}

static int printable_ascii(const char *text, size_t length) {
    for (size_t i = 0u; i < length; ++i)
        if ((unsigned char)text[i] < 0x20u || (unsigned char)text[i] > 0x7eu) return 0;
    return 1;
}

/* The public API on a C string pattern: 1, 0, or -1 when it is refused. */
static int search(const char *pattern, const char *value, size_t length) {
    maelys_cli_regex_t *regex = NULL;
    if (maelys_cli_regex_compile(pattern, strlen(pattern), &regex, NULL) != MAELYS_CLI_REGEX_OK)
        return -1;
    int matched = maelys_cli_regex_search(regex, value, length);
    maelys_cli_regex_free(regex);
    return matched;
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
        char *fields[5] = {NULL, NULL, NULL, NULL, NULL};
        size_t count = 0u;
        for (char *cursor = line; cursor && count < 5u; ) {
            fields[count++] = cursor;
            cursor = strchr(cursor, '\t');
            if (cursor) *cursor++ = '\0';
        }
        if (!strcmp(fields[0], "check") && (count == 3u || count == 5u)) {
            checks++;
            char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
            int ok = maelys_cli_pattern_check(fields[1], reason, sizeof(reason)) == 0;
            int expected = !strcmp(fields[2], "ok");
            /* The public API answers the family the corpus names, with a
             * reason and an offset inside the pattern. */
            maelys_cli_regex_t *regex = NULL;
            maelys_cli_regex_diagnostic_t diagnostic;
            maelys_cli_regex_status_t status = maelys_cli_regex_compile(
                fields[1], strlen(fields[1]), &regex, &diagnostic);
            maelys_cli_regex_free(regex);
            const char *family = (size_t)status < sizeof(family_names) / sizeof(family_names[0]) ?
                family_names[status] : "?";
            if (expected ? status != MAELYS_CLI_REGEX_OK || count != 3u :
                    count != 5u || strcmp(family, fields[3]) != 0 || !diagnostic.message ||
                    diagnostic.offset != strtoul(fields[4], NULL, 10)) {
                (void)fprintf(stderr, "%s:%d: compile '%s' gives %s at %zu (%s)\n", corpus_path,
                    number, fields[1], family, diagnostic.offset,
                    diagnostic.message ? diagnostic.message : "");
                failures++;
            }
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
            size_t value_length = unescape(fields[2]);
            int expected = !strcmp(fields[3], "yes");
            int matched = search(fields[1], fields[2], value_length);
            if (matched != expected) {
                (void)fprintf(stderr, "%s:%d: match '%s' on '%s' expected %s, got %d\n",
                    corpus_path, number, fields[1], fields[2], fields[3], matched);
                failures++;
            }
            if (strlen(fields[2]) == value_length && printable_ascii(fields[1], strlen(fields[1])) &&
                printable_ascii(fields[2], value_length) && anchors_at_ends(fields[1])) {
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

/* The public API at its corners: arguments it refuses, the offset and the
 * family of a refusal, which refusal wins, NUL bytes, and the handle. */
static int test_api(void) {
    maelys_cli_regex_t *regex = (maelys_cli_regex_t *)&regex;
    maelys_cli_regex_diagnostic_t d;
    CHECK(maelys_cli_regex_compile("a", 1u, NULL, &d) == MAELYS_CLI_REGEX_INVALID_ARGUMENT);
    CHECK(d.status == MAELYS_CLI_REGEX_INVALID_ARGUMENT && d.offset == 0u && d.message);
    CHECK(maelys_cli_regex_compile(NULL, 3u, &regex, &d) == MAELYS_CLI_REGEX_INVALID_ARGUMENT);
    CHECK(regex == NULL);
    CHECK(maelys_cli_regex_compile(NULL, 0u, &regex, &d) == MAELYS_CLI_REGEX_EMPTY);
    CHECK(regex == NULL && d.offset == 0u);
    CHECK(maelys_cli_regex_compile("abc", 0u, &regex, NULL) == MAELYS_CLI_REGEX_EMPTY);
    /* The offset: where reading stopped, the length for what the end tells. */
    CHECK(maelys_cli_regex_compile("^(a$", 4u, &regex, &d) == MAELYS_CLI_REGEX_GROUP);
    CHECK(d.offset == 4u && strstr(d.message, "unclosed group"));
    CHECK(maelys_cli_regex_compile("a)", 2u, &regex, &d) == MAELYS_CLI_REGEX_GROUP);
    CHECK(d.offset == 1u && strstr(d.message, "closes no group"));
    CHECK(maelys_cli_regex_compile("^[a-z", 5u, &regex, &d) == MAELYS_CLI_REGEX_BRACKET);
    CHECK(d.offset == 5u);
    CHECK(maelys_cli_regex_compile("^\\d$", 4u, &regex, &d) == MAELYS_CLI_REGEX_ESCAPE);
    CHECK(d.offset == 2u);
    CHECK(maelys_cli_regex_compile("^\xc3\xa9$", 4u, &regex, &d) == MAELYS_CLI_REGEX_ENCODING);
    CHECK(d.offset == 1u);
    /* A NUL byte of the pattern is a control character, at its offset;
     * the length is what is read, not the string. */
    CHECK(maelys_cli_regex_compile("x\0y", 3u, &regex, &d) == MAELYS_CLI_REGEX_ENCODING);
    CHECK(d.offset == 1u && strstr(d.message, "control character"));
    CHECK(maelys_cli_regex_compile("ab)", 2u, &regex, &d) == MAELYS_CLI_REGEX_OK);
    CHECK(regex && d.status == MAELYS_CLI_REGEX_OK && d.offset == 0u && d.message == NULL);
    CHECK(maelys_cli_regex_states(regex) == 3u);
    maelys_cli_regex_free(regex);
    /* The first refusal met from the left wins; the state budget is met
     * only once the whole pattern is read. */
    size_t big = 70000u;
    char *pattern = malloc(big + 3u);
    if (!pattern) return 0;
    memset(pattern, 'a', big);
    memcpy(pattern + big, "\\d", 3u);
    CHECK(maelys_cli_regex_compile(pattern, big + 2u, &regex, &d) == MAELYS_CLI_REGEX_ESCAPE);
    CHECK(d.offset == big + 1u);
    CHECK(maelys_cli_regex_compile(pattern, big, &regex, &d) == MAELYS_CLI_REGEX_LIMIT);
    CHECK(d.offset == big && strstr(d.message, "65536"));
    free(pattern);
    CHECK(maelys_cli_regex_compile("a{0,256}\\d", 10u, &regex, &d) == MAELYS_CLI_REGEX_LIMIT);
    CHECK(maelys_cli_regex_compile("\\d{0,256}", 10u, &regex, &d) == MAELYS_CLI_REGEX_ESCAPE);
    /* NUL in a value is a character; NULL is the empty value. */
    CHECK(maelys_cli_regex_compile("^a.b$", 5u, &regex, &d) == MAELYS_CLI_REGEX_OK);
    CHECK(maelys_cli_regex_search(regex, "a\0b", 3u) == 1);
    CHECK(maelys_cli_regex_search(regex, "a\0b", 2u) == 0);
    CHECK(maelys_cli_regex_search(regex, NULL, 0u) == 0);
    errno = 0;
    CHECK(maelys_cli_regex_search(regex, NULL, 1u) == -1 && errno == EINVAL);
    maelys_cli_regex_free(regex);
    errno = 0;
    CHECK(maelys_cli_regex_search(NULL, "a", 1u) == -1 && errno == EINVAL);
    CHECK(maelys_cli_regex_compile("^$", 2u, &regex, NULL) == MAELYS_CLI_REGEX_OK);
    CHECK(maelys_cli_regex_search(regex, NULL, 0u) == 1);
    maelys_cli_regex_free(regex);
    maelys_cli_regex_free(NULL);
    CHECK(maelys_cli_regex_states(NULL) == 0u);
    /* Every family has its sentence, and an unknown value one too. */
    for (int status = 0; status <= 10; ++status) {
        const char *text = maelys_cli_regex_status_text((maelys_cli_regex_status_t)status);
        CHECK(text && *text);
        for (int other = 0; other < status; ++other)
            CHECK(strcmp(text, maelys_cli_regex_status_text((maelys_cli_regex_status_t)other)) != 0);
    }
    CHECK(strstr(maelys_cli_regex_status_text((maelys_cli_regex_status_t)99), "unknown"));
    return 1;
}

#ifdef MAELYS_CLI_ALLOC_HOOK
/* Every allocation of a compile and of a search fails in turn: a compile
 * answers NO_MEMORY at offset 0 with no handle, a search -1 with ENOMEM,
 * and nothing leaks (the sanitizers of make asan-ubsan watch it). */
static int test_allocation_failures(void) {
    static const char pattern[] = "^(ab|c[d-f]x){2,5}[^0-9]*$";
    int failed_compiles = 0;
    for (int countdown = 0; ; ++countdown) {
        maelys_cli_regex_t *regex = (maelys_cli_regex_t *)&regex;
        maelys_cli_regex_diagnostic_t d;
        alloc_countdown = countdown;
        maelys_cli_regex_status_t status = maelys_cli_regex_compile(
            pattern, sizeof(pattern) - 1u, &regex, &d);
        int reached = alloc_countdown < 0;
        alloc_countdown = -1;
        if (status == MAELYS_CLI_REGEX_NO_MEMORY) {
            CHECK(regex == NULL && d.offset == 0u && d.message);
            CHECK(reached);
            failed_compiles++;
            continue;
        }
        CHECK(status == MAELYS_CLI_REGEX_OK && regex && !reached);
        alloc_countdown = 0;
        errno = 0;
        int matched = maelys_cli_regex_search(regex, "abcdx", 5u);
        alloc_countdown = -1;
        CHECK(matched == -1 && errno == ENOMEM);
        CHECK(maelys_cli_regex_search(regex, "abcdx", 5u) == 1);
        maelys_cli_regex_free(regex);
        break;
    }
    CHECK(failed_compiles >= 3);
    char reason[MAELYS_CLI_PATTERN_REASON_SIZE];
    alloc_countdown = 0;
    CHECK(maelys_cli_pattern_check("^a$", reason, sizeof(reason)) != 0);
    alloc_countdown = -1;
    CHECK(strstr(reason, "out of memory"));
    return 1;
}

/* The generation that marks the positions of a step is a uint32_t, one per
 * code point read: past 2^32 - 1 of them it wraps. Started next to the wrap,
 * every search of the corpus crosses it within its first characters, and
 * answers as it does from 0. */
static int test_generation_wrap(void) {
    static const uint32_t starts[] = {UINT32_MAX - 2u, UINT32_MAX - 1u, UINT32_MAX};
    for (size_t i = 0u; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        maelys_cli_test_generation = starts[i];
        int ok = test_corpus() && test_bytes();
        maelys_cli_test_generation = 0u;
        CHECK(ok);
    }
    return 1;
}
#endif

/* One handle, searched by eight threads at once: it is never written after
 * compile, and every thread gets the answers one thread gets
 * (make tsan-check runs this test under ThreadSanitizer). */
typedef struct {
    const maelys_cli_regex_t *regex;
    int ok;
} shared_search_t;

static const char *const shared_values[] = {
    "note.write", "Note", "n", "note.", "a-b.c-d", "", "x\xff", "-a",
};
static const int shared_expected[] = {1, 0, 1, 0, 1, 0, 0, 0};

static void *search_shared(void *argument) {
    shared_search_t *shared = argument;
    shared->ok = 1;
    for (int round = 0; round < 500; ++round)
        for (size_t i = 0u; i < sizeof(shared_values) / sizeof(shared_values[0]); ++i)
            if (maelys_cli_regex_search(shared->regex, shared_values[i],
                    strlen(shared_values[i])) != shared_expected[i])
                shared->ok = 0;
    return NULL;
}

static int test_concurrent_searches(void) {
    static const char pattern[] = "^[a-z]([a-z0-9.-]*[a-z0-9-])?$";
    maelys_cli_regex_t *regex = NULL;
    CHECK(maelys_cli_regex_compile(pattern, sizeof(pattern) - 1u, &regex, NULL) ==
        MAELYS_CLI_REGEX_OK);
    pthread_t threads[8];
    shared_search_t shared[8];
    for (int i = 0; i < 8; ++i) {
        shared[i].regex = regex;
        shared[i].ok = 0;
        CHECK(pthread_create(&threads[i], NULL, search_shared, &shared[i]) == 0);
    }
    int ok = 1;
    for (int i = 0; i < 8; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        ok = ok && shared[i].ok;
    }
    maelys_cli_regex_free(regex);
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
    RUN(test_api);
#ifdef MAELYS_CLI_ALLOC_HOOK
    RUN(test_allocation_failures);
    RUN(test_generation_wrap);
#endif
    RUN(test_concurrent_searches);
    return failures ? 1 : 0;
}
