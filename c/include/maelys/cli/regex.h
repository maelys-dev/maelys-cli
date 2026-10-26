#ifndef MAELYS_CLI_REGEX_H
#define MAELYS_CLI_REGEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The subset of regular expressions agent-cli/v2 writes an argument.pattern
 * in (section 3): printable ASCII, literals and `\` before one of the
 * fourteen metacharacters, `.`, classes of ranges, plain groups,
 * alternation, `*`, `+`, `?`, `{m}`, `{m,}`, `{m,n}` with n at most 255,
 * `^` and `$`. The library reads it and matches it itself, the reader of
 * every argument.pattern of a catalog; a product may hand it any other
 * pattern written in that subset.
 *
 * Reading: a value is a sequence of code points, its UTF-8; a byte that
 * belongs to no valid sequence (shortest form, at most U+10FFFF, no
 * surrogate) is one character that only `.` and a negated class match; an
 * octet NUL is a character, U+0000. `.` matches every character but
 * U+000A, U+000D, U+2028 and U+2029. `^` and `$` hold at the two ends of the
 * value only, `$` never before a final newline. A value matches when some
 * part of it does: a pattern without anchors matches anywhere.
 *
 * Cost: a search is a Thompson simulation of the pattern's program, never a
 * backtracking: O((length + 1) x states) in time and O(states) in memory,
 * allocated per search or given by the caller (a scratch), states being at
 * most MAELYS_CLI_REGEX_MAX_STATES. A compilation is
 * O(pattern length + states). The depth of every recursion is the nesting
 * of groups, never the length of the pattern.
 */

#define MAELYS_CLI_REGEX_MAX_BOUND 255u     /* n of {m,n}: a rule of syntax */
#define MAELYS_CLI_REGEX_MAX_DEPTH 32u      /* nested groups: a budget */
#define MAELYS_CLI_REGEX_MAX_STATES 65536u  /* program once expanded: a budget */

/* Stable values, never renumbered; a new status takes the next value. */
typedef enum {
    MAELYS_CLI_REGEX_OK = 0,
    MAELYS_CLI_REGEX_INVALID_ARGUMENT = 1, /* out NULL, or pattern NULL with a length */
    MAELYS_CLI_REGEX_NO_MEMORY = 2,
    MAELYS_CLI_REGEX_EMPTY = 3,            /* an empty pattern, alternative or group */
    MAELYS_CLI_REGEX_ENCODING = 4,         /* a character outside printable ASCII */
    MAELYS_CLI_REGEX_ESCAPE = 5,           /* `\` before no metacharacter: \d, \n, \- */
    MAELYS_CLI_REGEX_BRACKET = 6,          /* a class outside the subset */
    MAELYS_CLI_REGEX_GROUP = 7,            /* (? , an unclosed group, a stray ), ] or } */
    MAELYS_CLI_REGEX_QUANTIFIER = 8,       /* nothing to repeat, two in a row, on an anchor */
    MAELYS_CLI_REGEX_BOUND = 9,            /* not {m}, {m,} or {m,n} in decimal, m above n */
    MAELYS_CLI_REGEX_LIMIT = 10            /* a bound above 255, the depth or the states */
} maelys_cli_regex_status_t;

/* Why a pattern was refused. `status` is stable; `message` is a sentence
 * for a person, static and never freed, whose wording may change from one
 * release to the next: a program compares `status`, never `message`.
 * `offset` is the byte of the pattern where reading stopped, from 0 to its
 * length (the length for what the end reveals, an unclosed group or the
 * state budget). When several refusals apply, the first one met reading
 * from left to right is reported; the state budget is met only once the
 * whole pattern is read. On success: {OK, 0, NULL}; on NO_MEMORY and
 * INVALID_ARGUMENT the offset is 0. */
typedef struct maelys_cli_regex_diagnostic {
    maelys_cli_regex_status_t status;
    size_t offset;
    const char *message;
} maelys_cli_regex_diagnostic_t;

/* A compiled pattern. Immutable once compiled: searches may share it
 * across threads. */
typedef struct maelys_cli_regex maelys_cli_regex_t;

/* Reads `length` bytes of `pattern` (which need not be NUL-terminated; an
 * octet NUL in it is a control character, refused with ENCODING). On
 * success *out is a handle to free with maelys_cli_regex_free; otherwise
 * *out is NULL. A length of 0 is EMPTY, pattern NULL included. `diagnostic`
 * may be NULL. */
maelys_cli_regex_status_t maelys_cli_regex_compile(
    const char *pattern, size_t length, maelys_cli_regex_t **out,
    maelys_cli_regex_diagnostic_t *diagnostic);

/* 1 when some part of the `length` bytes of `value` matches, 0 when none
 * does, -1 with errno EINVAL (regex NULL, or value NULL with a length) or
 * ENOMEM (its working lists, O(states), are allocated per call). value NULL
 * with a length of 0 is the empty value. A search that must not allocate
 * is maelys_cli_regex_search_scratch. */
int maelys_cli_regex_search(
    const maelys_cli_regex_t *regex, const char *value, size_t length);

/* The bytes a search of this regex works in, for
 * maelys_cli_regex_search_scratch: O(states), bounded through
 * MAELYS_CLI_REGEX_MAX_STATES. This function is the only authority on the
 * size; the formula behind it may change from one release to the next.
 * 0 for NULL. A scratch of max(maelys_cli_regex_scratch_size(regex_i))
 * bytes over the regexes a caller searches serves all of them. */
size_t maelys_cli_regex_scratch_size(const maelys_cli_regex_t *regex);

/* As maelys_cli_regex_search, the same answer for every value, in the
 * caller's `scratch` of `scratch_size` bytes. It never allocates: a search
 * whose preconditions hold has no failure of resources. 1 when some part of
 * the `length` bytes of `value` matches, 0 when none does, -1 with errno
 * EINVAL (regex NULL, value NULL with a length, scratch NULL, or
 * scratch_size below maelys_cli_regex_scratch_size(regex)). value NULL
 * with a length of 0 is the empty value.
 *
 * The scratch is writable storage of scratch_size bytes, of no particular
 * type, a declared array of char included: it needs no alignment and no
 * initialization, and holds nothing a later search reads; its content after
 * a search is unspecified. It must not overlap `value` or the regex. One
 * scratch serves one search at a time: the regex may be shared across
 * threads, and a scratch may pass from one thread to another between
 * searches, with the synchronization that hands it over, but never serve
 * two searches at once. What the library cannot detect -- a dangling
 * scratch, a scratch_size larger than the storage, a scratch searched in by
 * two threads at once -- is undefined behaviour, as for any buffer. */
int maelys_cli_regex_search_scratch(
    const maelys_cli_regex_t *regex, void *scratch, size_t scratch_size,
    const char *value, size_t length);

/* The size of the compiled program, which bounds the work of a search;
 * 0 for NULL. */
size_t maelys_cli_regex_states(const maelys_cli_regex_t *regex);

/* NULL is accepted and does nothing. */
void maelys_cli_regex_free(maelys_cli_regex_t *regex);

/* A sentence for a person that names the family of a status, static; its
 * wording may change from one release to the next. */
const char *maelys_cli_regex_status_text(maelys_cli_regex_status_t status);

#ifdef __cplusplus
}
#endif

#endif
