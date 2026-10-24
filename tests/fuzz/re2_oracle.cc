// SPDX-License-Identifier: MPL-2.0
/* The reference engine of fuzz_pattern under `make fuzz`: RE2, read with
 * POSIX syntax. For whether a value matches, the common subset of
 * ECMA-262 and POSIX ERE describes a regular language, so every correct
 * engine answers alike; RE2 is the one that answers in linear time and
 * bounded memory whatever the pattern, and refuses rather than expands a
 * pattern beyond its limits. The libc is no such witness: glibc's regcomp
 * spends gigabytes on nested bounds, exponential time on a repeated group
 * that can match nothing, and reads an anchor inside a repeated group
 * wrongly. Built only by `make fuzz`; `make fuzz-smoke` replays the harness
 * without it, on every leg. */
#include <re2/re2.h>

#include <string>

extern "C" int maelys_cli_fuzz_re2_match(const char *pattern, const char *value);

/* RE2 reads `.` as every code point but U+000A; agent-cli/v2 (section 3)
 * reads it as ECMA-262 does, every code point but the four line
 * terminators, and names the translation for such an engine:
 * [^\n\r U+2028 U+2029]. A pattern of the subset escapes its metacharacters
 * with `\` and holds no `\` and no `[` inside a class, so an unescaped `.`
 * outside a class is the only one to translate. */
static std::string ecma_dot(const char *pattern) {
    std::string out;
    bool in_class = false;
    for (const char *p = pattern; *p; ++p) {
        if (!in_class && *p == '\\' && p[1]) {
            out += *p++;
            out += *p;
        } else if (!in_class && *p == '[') {
            in_class = true;
            out += *p;
            if (p[1] == '^') out += *++p;
        } else if (in_class && *p == ']') {
            in_class = false;
            out += *p;
        } else if (!in_class && *p == '.') {
            out += "[^\n\r\xe2\x80\xa8\xe2\x80\xa9]";
        } else {
            out += *p;
        }
    }
    return out;
}

/* 1 when the value matches (a search), 0 when it does not, -1 when RE2
 * refuses the pattern as a syntax error, -2 when it is beyond RE2's limits
 * (a repetition RE2 will not expand, a program above its memory). */
int maelys_cli_fuzz_re2_match(const char *pattern, const char *value) {
    RE2::Options options;
    options.set_posix_syntax(true);
    options.set_one_line(true);      /* ^ and $ at the ends of the value only */
    options.set_never_nl(false);     /* a value may hold a line terminator */
    options.set_longest_match(true);
    options.set_log_errors(false);
    options.set_max_mem(8 << 20);
    RE2 compiled(ecma_dot(pattern), options);
    if (!compiled.ok()) {
        switch (compiled.error_code()) {
        case RE2::ErrorRepeatSize:
        case RE2::ErrorPatternTooLarge:
            return -2;
        default:
            return -1;
        }
    }
    return RE2::PartialMatch(value, compiled) ? 1 : 0;
}
