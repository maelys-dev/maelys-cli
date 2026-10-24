#ifndef MAELYS_CLI_INTERNAL_H
#define MAELYS_CLI_INTERNAL_H

#include "maelys/cli/app.h"
#include "maelys/cli/catalog.h"
#include "maelys/cli/invocation.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Transport options accepted by every command with an envelope output. */
const maelys_cli_option_t *maelys_cli_transport_options(size_t *out_count);

/* Iterates built-in commands followed by the product catalog. */
size_t maelys_cli_app_command_count(const maelys_cli_app_t *app);
const maelys_cli_command_t *maelys_cli_app_command_at(
    const maelys_cli_app_t *app, size_t index);
const maelys_cli_command_t *maelys_cli_app_find_command(
    const maelys_cli_app_t *app, const char *id);

size_t maelys_cli_pattern_words(const char *pattern);

/* The synopsis help shows, owned by the caller: the pattern, the operands
 * with their arity, the required options and "[options]" for the others;
 * a declared synopsis as written. describe keeps the full one. NULL when it
 * does not fit or memory runs out. */
char *maelys_cli_command_help_synopsis_alloc(const maelys_cli_command_t *command);

/* The rendering refusals of `command` for the options `invocation` holds: a
 * stream refuses every rendering flag, --format jsonl is for records unless
 * --field is given, --field conflicts with an explicit json. Sets `error`
 * and returns 1 when one applies. The parser asks it of the command a line
 * runs; the runner asks it of `help` when a line asks for help instead,
 * which the parser, stopping at --help, did not. */
int maelys_cli_rendering_refused(
    const maelys_cli_command_t *command, const maelys_cli_invocation_t *invocation,
    maelys_cli_error_t *error);

/* Writes user-influenced text without allowing terminal control bytes. */
void maelys_cli_fprint_terminal_safe(FILE *stream, const char *text);

/* True when text contains valid UTF-8 without terminal control code points. */
int maelys_cli_text_is_terminal_safe(const char *text);

/* The language of human text (src/language.c). A tag is well formed as
 * agent-cli/v2 section 5 writes it, language[-Script][-REGION], the subset
 * of RFC 5646 MAELYS_CLI_LANGUAGE_TAG_PATTERN spells, compared without
 * regard to case. A locale
 * converts to a tag (1), to the canonical language (0: C, POSIX, C.*, or
 * one that does not convert) or is empty (-1). Lookup is RFC 4647 section
 * 3.4 against the languages a program has, NULL meaning the canonical one.
 * Resolve takes the first source that is not empty, in the order the
 * contract writes, and never reads a later one. */
/* The canonical language of the framework's and a product's human text,
 * which `version` names (`lang`). */
#define MAELYS_CLI_CANONICAL_LANGUAGE "en"
#define MAELYS_CLI_LANGUAGE_TAG_PATTERN \
    "^[A-Za-z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?$"
int maelys_cli_language_tag_is_well_formed(const char *tag);
int maelys_cli_language_from_locale(const char *locale, char *tag, size_t size);
const char *maelys_cli_language_lookup(const char *tag, const char *const *available,
                                       size_t count);
const char *maelys_cli_language_resolve(
    const char *option, const char *variable, const char *lc_all,
    const char *lc_messages, const char *lang,
    const char *const *available, size_t count);

/* maelys_cli_error_set with its arguments as a va_list: the template and
 * its formatted arguments are kept for the text rendering. */
void maelys_cli_error_setv(
    maelys_cli_error_t *error, const char *code, const char *hint,
    const char *format, va_list arguments);

/* Translations (src/language.c). translate returns the product's text for
 * `source`, else the framework's, else `source` itself. A template's
 * conversions are listed by template_conversions ("%s %llu"), which a
 * translation must keep in order. translate_error writes the text
 * rendering of an error's message in the language: its template
 * translated and its arguments put back, or a message a table names whole;
 * 0 when there is none. framework_language is the framework's own table
 * for the language a request resolved to, NULL for English. */
const char *maelys_cli_translate(const maelys_cli_language_t *product,
                                 const maelys_cli_language_t *framework,
                                 const char *source);
int maelys_cli_template_conversions(const char *format, char *out, size_t size);
int maelys_cli_translate_error(const maelys_cli_language_t *product,
                               const maelys_cli_language_t *framework,
                               const maelys_cli_error_t *error, char *out, size_t size);
const maelys_cli_language_t *maelys_cli_framework_language(const char *tag);

/* argument.pattern: the common subset of ECMA-262 and POSIX ERE (spec 2.3),
 * read and matched by src/pattern.c itself, no regcomp(). check returns 0,
 * or -1 with the reason the pattern is outside the subset written in
 * `reason` (MAELYS_CLI_PATTERN_REASON_SIZE bytes suffice); match returns 1
 * when the value matches (a search, as regexec() and re.search() do), 0 when
 * it does not, -1 when the pattern is outside the subset. */
#define MAELYS_CLI_PATTERN_REASON_SIZE 128u
int maelys_cli_pattern_check(const char *pattern, char *reason, size_t reason_size);
int maelys_cli_pattern_match(const char *pattern, const char *value);
/* match, the work it did and the size of the program it ran, for tests. The
 * work is the positions popped and the threads tried: at each position of
 * the value, and once before it, every instruction is tried once and
 * expanded once, and an expansion pushes two positions at most, so the work
 * is at most (value length + 1) x (4 x states + 1). */
int maelys_cli_pattern_match_counted(const char *pattern, const char *value,
                                     uint64_t *steps, uint32_t *states);

/* JSON spans for the text rendering of records: the end of the value that
 * starts at `offset` (0 when malformed), and a decoded copy of a string. */
size_t maelys_cli_json_value_end(const char *text, size_t length, size_t offset);
char *maelys_cli_json_string_decode(const char *text, size_t length, size_t offset);

/* Validates text against an option's kind; fills the typed value. */
int maelys_cli_option_validate_text(
    const maelys_cli_option_t *option, const char *value,
    maelys_cli_parsed_option_t *parsed, maelys_cli_error_t *error);

/* Set by maelys_cli_main() so delegates can be resolved beside the binary. */
extern const char *maelys_cli_argv0;

#endif
