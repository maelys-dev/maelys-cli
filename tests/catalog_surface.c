/* SPDX-License-Identifier: MPL-2.0 */
/* A catalog that uses every declaration macro of catalog.h and every
 * descriptor field the framework serializes, so that `describe` can be
 * validated against the pinned specification's own schema.
 *
 * scripts/describe-schema-check.py runs this program and validates what it
 * emits; it also refuses to pass while a macro of catalog.h is missing
 * here, so a macro added later cannot escape the check. The reference
 * products exercise what a product plausibly needs; this one exercises
 * what the framework can emit, which is a larger set — MAELYS_CLI_HEX_OR
 * was serialized as an `alternativeDigits` member no product used and no
 * schema allowed. */
#include "maelys/cli.h"

#include <stddef.h>

static int reply(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", NULL, MAELYS_CLI_EXIT_OK);
}

static const char *const levels[] = {"quiet", "normal", "loud", NULL};
static const char *const algorithms[] = {"sha256", "sha512", NULL};
static const char *const together[] = {"paired-a", "paired-b", NULL};

#define SURFACE_LIMIT 10

/* Every value kind, and every option field beside them: required,
 * repeatable, hidden, depends_on, depends_on_all, conflicts_with, group,
 * pattern, default_text through MAELYS_CLI_DEFAULT_OF. */
static const maelys_cli_option_t surface_options[] = {
    {MAELYS_CLI_FLAG("plain", "A flag, the simplest option.")},
    {MAELYS_CLI_FLAG("hidden-flag", "Parsed and described, never shown."),
     .hidden = 1},
    {MAELYS_CLI_STRING("text", "TEXT", "A free string, repeatable."),
     .repeatable = 1},
    {MAELYS_CLI_STRING("named", "NAME", "A string constrained by a pattern."),
     .pattern = "^[a-z][a-z0-9-]*$"},
    {MAELYS_CLI_PATH("file", "FILE", "A path.")},
    {MAELYS_CLI_ABSOLUTE_PATH("root", "DIR", "An absolute path.")},
    {MAELYS_CLI_UNSIGNED("count", "N", "An unsigned bounded value.", 1, SURFACE_LIMIT),
     MAELYS_CLI_DEFAULT_OF(SURFACE_LIMIT)},
    {MAELYS_CLI_INTEGER("offset", "N", "A signed bounded value.", -100, 100)},
    {MAELYS_CLI_SIZE("memory", "BYTES", "A size with K/M/G accepted.", 1, 0)},
    {MAELYS_CLI_DURATION("wall-time", "DURATION", "A duration with its unit.", 0, 0)},
    {MAELYS_CLI_CHOICE("level", "A closed set of values.", levels),
     .default_text = "normal"},
    {MAELYS_CLI_HEX("short-digest", "HEX", "A hex value of one length.", 40)},
    {MAELYS_CLI_HEX_OR("digest", "HEX", "A hex value of either length.", 40, 64)},
    {MAELYS_CLI_DIGEST("checksum", "DIGEST", "An algorithm-prefixed digest.",
     algorithms)},
    {MAELYS_CLI_FLAG("strict", "Requires another option."),
     .depends_on = "level"},
    {MAELYS_CLI_FLAG("lenient", "Conflicts with another option."),
     .conflicts_with = "strict"},
    {MAELYS_CLI_FLAG("combined", "Requires every listed option."),
     .depends_on_all = together},
    {MAELYS_CLI_FLAG("paired-a", "Given with paired-b or not at all."),
     .group = "pair"},
    {MAELYS_CLI_FLAG("paired-b", "Given with paired-a or not at all."),
     .group = "pair"},
    {MAELYS_CLI_STRING("mandatory", "TEXT", "A required option."),
     .required = 1},
};

/* Every operand macro, in the order the parser accepts them: required
 * first, then typed, then optional, then the variadic rest. */
static const maelys_cli_operand_t surface_operands[] = {
    {MAELYS_CLI_OPERAND("NAME", "A required operand.")},
    {MAELYS_CLI_OPERAND_CHOICE("LEVEL", "A required operand of a closed set.",
     levels)},
    {MAELYS_CLI_OPERAND_KIND("PATH", "A required operand of a value kind.",
     MAELYS_CLI_VALUE_ABSOLUTE_PATH)},
    {MAELYS_CLI_OPERAND_OPTIONAL("EXTRA", "An optional operand.")},
    {MAELYS_CLI_OPERAND_REST("REST", "Every remaining operand.")},
};

/* A transaction declares its own --apply; the framework refuses the
 * declaration without it. */
static const maelys_cli_option_t apply_options[] = {
    {MAELYS_CLI_FLAG("force", "An ordinary flag beside --apply.")},
    MAELYS_CLI_APPLY_OPTION,
};

static const maelys_cli_operand_t one_operand[] = {
    {MAELYS_CLI_OPERAND("TARGET", "A required operand.")},
};

static const char surface_schema[] =
    "{\"type\":\"object\",\"additionalProperties\":false}";

static const maelys_cli_command_t commands[] = {
    /* Every output mode and effect, with the fields that only some carry:
     * an output schema, an explicit synopsis, hidden, a protocol name, an
     * unavailable reason and a delegate. */
    {MAELYS_CLI_READ("read", "read", "A read with the whole option surface.",
     reply),
     MAELYS_CLI_OPERANDS(surface_operands), MAELYS_CLI_OPTIONS(surface_options),
     MAELYS_CLI_SCHEMA(surface_schema)},
    {MAELYS_CLI_RECORDS("records", "records", "A records command.", reply),
     MAELYS_CLI_SCHEMA(surface_schema)},
    {MAELYS_CLI_TRANSACTION("plan.apply", "plan apply",
     "A transaction that plans and applies.", reply),
     MAELYS_CLI_OPERANDS(one_operand), MAELYS_CLI_OPTIONS(apply_options)},
    {MAELYS_CLI_COMMIT_TRANSACTION("plan.commit", "plan commit",
     "A transaction that plans and commits.", reply),
     MAELYS_CLI_OPERANDS(one_operand), MAELYS_CLI_OPTIONS(apply_options)},
    {MAELYS_CLI_EXECUTE("execute", "execute", "An execute command.", reply)},
    {MAELYS_CLI_STREAM("stream", "stream", "A stream that owns stdio.", reply),
     MAELYS_CLI_OPERANDS(one_operand),
     .synopsis = "stream TARGET | stream -- TARGET"},
    {MAELYS_CLI_PROTOCOL_STREAM("protocol", "protocol",
     "A stream that names its protocol.", reply, "surface/v1")},
    {MAELYS_CLI_EXTERNAL("external", "external",
     "A command delegated to a helper.", "maelys-surface-helper"),
     MAELYS_CLI_OPERANDS(one_operand)},
    {MAELYS_CLI_READ("hidden", "hidden", "Described, never shown.", reply),
     .hidden = 1},
    /* An unavailable command names its reason and carries no handler. */
    {MAELYS_CLI_READ("absent", "absent", "Declared, not built here.", NULL),
     .unavailable = "built without the surface backend"},
};

int main(int argc, char **argv) {
    static const maelys_cli_app_t app = {
        .program = "catalog-surface",
        .product = "Catalog surface",
        .version = "0.0.0",
        .summary = "every declaration the framework can serialize",
        .commands = commands,
        .command_count = MAELYS_CLI_COUNT(commands),
    };
    return maelys_cli_main(&app, argc, argv);
}
