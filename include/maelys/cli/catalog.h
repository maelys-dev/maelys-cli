#ifndef MAELYS_CLI_CATALOG_H
#define MAELYS_CLI_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The declarative command catalog. It is the only source used for parsing,
 * `help`, machine-readable `describe`, reference generation and tests.
 */

typedef enum maelys_cli_value_kind {
    MAELYS_CLI_VALUE_NONE = 0, /* boolean flag; accepts --flag=false */
    MAELYS_CLI_VALUE_STRING,
    MAELYS_CLI_VALUE_INTEGER,  /* signed decimal within [minimum, maximum] */
    MAELYS_CLI_VALUE_UNSIGNED, /* unsigned decimal within [minimum, maximum] */
    MAELYS_CLI_VALUE_SIZE,     /* bytes, K/M/G/T suffix accepted */
    MAELYS_CLI_VALUE_DURATION, /* ms/s/m/h/d suffix required; milliseconds */
    MAELYS_CLI_VALUE_PATH,     /* non-empty filesystem path */
    MAELYS_CLI_VALUE_CHOICE,   /* one of `choices` */
    MAELYS_CLI_VALUE_HEX,      /* lowercase hex of `hex_digits` digits */
    MAELYS_CLI_VALUE_ABSOLUTE_PATH, /* path starting with '/' */
    MAELYS_CLI_VALUE_DIGEST    /* ALGORITHM:HEX, algorithm in `choices` */
} maelys_cli_value_kind_t;

typedef enum maelys_cli_effect {
    MAELYS_CLI_EFFECT_NONE = 0, /* only valid as apply_effect */
    MAELYS_CLI_EFFECT_READ,     /* inspects state, never writes */
    MAELYS_CLI_EFFECT_PREVIEW,  /* computes an exact plan, never writes */
    MAELYS_CLI_EFFECT_APPLY,    /* persists one reviewed transaction */
    MAELYS_CLI_EFFECT_COMMIT,   /* records a reviewed version-control commit */
    MAELYS_CLI_EFFECT_EXECUTE,  /* runs a non-transactional action */
    MAELYS_CLI_EFFECT_STREAM    /* reserves stdio for a protocol */
} maelys_cli_effect_t;

typedef enum maelys_cli_output_mode {
    MAELYS_CLI_OUTPUT_ENVELOPE = 0, /* human text or one JSON envelope */
    MAELYS_CLI_OUTPUT_RECORDS,      /* zero or more records; jsonl capable */
    MAELYS_CLI_OUTPUT_STREAM        /* stdout belongs to a declared protocol */
} maelys_cli_output_mode_t;

typedef struct maelys_cli_operand {
    const char *name;    /* UPPER_CASE placeholder */
    const char *summary;
    int required;
    int variadic;        /* absorbs all remaining operands, including -- */
    /* Optional typing, validated like an option value. NONE means any
     * non-empty text. A variadic operand applies its type to every value. */
    maelys_cli_value_kind_t kind;
    const char *const *choices;     /* CHOICE and DIGEST */
    uint64_t minimum;               /* UNSIGNED, SIZE, DURATION */
    uint64_t maximum;               /* 0 means UINT64_MAX */
    int64_t signed_minimum;         /* INTEGER */
    int64_t signed_maximum;
    size_t hex_digits;              /* HEX */
    size_t hex_digits_alternative;
} maelys_cli_operand_t;

typedef struct maelys_cli_option {
    const char *name;               /* long spelling without dashes */
    maelys_cli_value_kind_t kind;
    const char *value_name;         /* placeholder in synopsis */
    const char *summary;
    int required;
    int repeatable;
    const char *depends_on;           /* option that must also be present */
    const char *conflicts_with;     /* option that must be absent */
    const char *const *choices;     /* NULL-terminated, CHOICE only */
    uint64_t minimum;               /* UNSIGNED, SIZE, DURATION */
    uint64_t maximum;               /* 0 means UINT64_MAX */
    int64_t signed_minimum;         /* INTEGER; both zero means full range */
    int64_t signed_maximum;
    size_t hex_digits;              /* HEX */
    const char *default_text;       /* default value, validated against the
                                       kind at startup and returned by the
                                       typed accessors when absent */
    size_t hex_digits_alternative;  /* HEX: second accepted length, 0 = none */
    const char *const *depends_on_all; /* NULL-terminated: every listed
                                          option must also be present */
    const char *group;              /* all-or-none group: options sharing a
                                       group name are given together or not
                                       at all */
} maelys_cli_option_t;

struct maelys_cli_context;
typedef int (*maelys_cli_handler_t)(struct maelys_cli_context *context);

typedef struct maelys_cli_command {
    const char *id;        /* stable dotted identifier: repo.init */
    const char *pattern;   /* space-separated words: "repo init" */
    const char *purpose;
    maelys_cli_effect_t effect;
    maelys_cli_effect_t apply_effect; /* NONE unless plan/apply transaction */
    maelys_cli_output_mode_t output;
    const maelys_cli_operand_t *operands;
    size_t operand_count;
    const maelys_cli_option_t *options;
    size_t option_count;
    const char *output_schema_json;   /* JSON Schema of data; NULL = object */
    maelys_cli_handler_t handler;     /* built-in implementation */
    const char *delegate;             /* absolute path or helper name */
    const char *synopsis;             /* NULL derives it from the catalog */
    int hidden;                       /* omitted from help, kept in describe */
    const char *protocol;             /* STREAM only: name of the protocol
                                         owning stdio, e.g. "git-smart" */
    const char *unavailable;          /* reason when this build has neither
                                         handler nor delegate; the command
                                         stays described and fails with
                                         UNSUPPORTED */
} maelys_cli_command_t;

/* Upper bound of a derived synopsis; the catalog validation names the
 * command whose synopsis would exceed it. */
#define MAELYS_CLI_MAX_SYNOPSIS 4096u

#define MAELYS_CLI_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define MAELYS_CLI_OPERANDS(array) \
    .operands = (array), .operand_count = MAELYS_CLI_COUNT(array)
#define MAELYS_CLI_OPTIONS(array) \
    .options = (array), .option_count = MAELYS_CLI_COUNT(array)

/*
 * Declaration helpers. Each macro expands to the designated fields of one
 * descriptor, without braces, so a declaration adds its own attributes:
 *
 *   static const maelys_cli_option_t note_options[] = {
 *       {MAELYS_CLI_STRING("content", "TEXT", "Text to store."), .required = 1},
 *       {MAELYS_CLI_SIZE("memory", "BYTES", "Ceiling.", 1u, 0u), .default_text = "1G"},
 *       MAELYS_CLI_APPLY_OPTION,
 *   };
 *   static const maelys_cli_command_t commands[] = {
 *       {MAELYS_CLI_TRANSACTION("note.write", "note write", "Store a note.", note_write),
 *        MAELYS_CLI_OPERANDS(note_operands), MAELYS_CLI_OPTIONS(note_options),
 *        MAELYS_CLI_SCHEMA(note_write_schema)},
 *   };
 */

/* Operands. */
#define MAELYS_CLI_OPERAND(name_, summary_) \
    .name = (name_), .summary = (summary_), .required = 1, .variadic = 0
#define MAELYS_CLI_OPERAND_OPTIONAL(name_, summary_) \
    .name = (name_), .summary = (summary_), .required = 0, .variadic = 0
#define MAELYS_CLI_OPERAND_REST(name_, summary_) \
    .name = (name_), .summary = (summary_), .required = 0, .variadic = 1
/* Typed operands: add .required/.variadic and kind-specific limits after. */
#define MAELYS_CLI_OPERAND_CHOICE(name_, summary_, choices_) \
    .name = (name_), .summary = (summary_), .required = 1, \
    .kind = MAELYS_CLI_VALUE_CHOICE, .choices = (choices_)
#define MAELYS_CLI_OPERAND_KIND(name_, summary_, kind_) \
    .name = (name_), .summary = (summary_), .required = 1, .kind = (kind_)

/* Options: name, value placeholder, summary, then kind-specific limits.
 * A maximum of 0 means unbounded. */
#define MAELYS_CLI_FLAG(name_, summary_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_NONE, .summary = (summary_)
#define MAELYS_CLI_STRING(name_, value_, summary_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_STRING, .value_name = (value_), \
    .summary = (summary_)
#define MAELYS_CLI_PATH(name_, value_, summary_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_PATH, .value_name = (value_), \
    .summary = (summary_)
#define MAELYS_CLI_UNSIGNED(name_, value_, summary_, minimum_, maximum_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_UNSIGNED, .value_name = (value_), \
    .summary = (summary_), .minimum = (minimum_), .maximum = (maximum_)
#define MAELYS_CLI_SIZE(name_, value_, summary_, minimum_, maximum_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_SIZE, .value_name = (value_), \
    .summary = (summary_), .minimum = (minimum_), .maximum = (maximum_)
#define MAELYS_CLI_DURATION(name_, value_, summary_, minimum_, maximum_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_DURATION, .value_name = (value_), \
    .summary = (summary_), .minimum = (minimum_), .maximum = (maximum_)
#define MAELYS_CLI_INTEGER(name_, value_, summary_, minimum_, maximum_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_INTEGER, .value_name = (value_), \
    .summary = (summary_), .signed_minimum = (minimum_), \
    .signed_maximum = (maximum_)
#define MAELYS_CLI_CHOICE(name_, summary_, choices_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_CHOICE, .summary = (summary_), \
    .choices = (choices_)
#define MAELYS_CLI_HEX(name_, value_, summary_, digits_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_HEX, .value_name = (value_), \
    .summary = (summary_), .hex_digits = (digits_)
#define MAELYS_CLI_ABSOLUTE_PATH(name_, value_, summary_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_ABSOLUTE_PATH, \
    .value_name = (value_), .summary = (summary_)
/* Prefixed digest such as sha256:HEX. algorithms_ is a NULL-terminated list
 * of accepted names among sha1, sha256, sha384 and sha512; the hexadecimal
 * length is implied by the algorithm. */
#define MAELYS_CLI_DIGEST(name_, value_, summary_, algorithms_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_DIGEST, .value_name = (value_), \
    .summary = (summary_), .choices = (algorithms_)
/* Hexadecimal accepting either of two lengths (any two, e.g. 40 and 64). */
#define MAELYS_CLI_HEX_OR(name_, value_, summary_, digits_, alternative_) \
    .name = (name_), .kind = MAELYS_CLI_VALUE_HEX, .value_name = (value_), \
    .summary = (summary_), .hex_digits = (digits_), \
    .hex_digits_alternative = (alternative_)

/* The standard transactional switch, shared by every plan/apply command. */
#define MAELYS_CLI_APPLY_OPTION \
    {MAELYS_CLI_FLAG("apply", \
     "Apply the reviewed transaction; omission returns a read-only plan.")}

/* Commands: identity plus effect and output mode; add operands, options,
 * schema and attributes after the macro. */
#define MAELYS_CLI_READ(id_, pattern_, purpose_, handler_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_READ, .output = MAELYS_CLI_OUTPUT_ENVELOPE, \
    .handler = (handler_)
#define MAELYS_CLI_RECORDS(id_, pattern_, purpose_, handler_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_READ, .output = MAELYS_CLI_OUTPUT_RECORDS, \
    .handler = (handler_)
#define MAELYS_CLI_TRANSACTION(id_, pattern_, purpose_, handler_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_PREVIEW, .apply_effect = MAELYS_CLI_EFFECT_APPLY, \
    .output = MAELYS_CLI_OUTPUT_ENVELOPE, .handler = (handler_)
#define MAELYS_CLI_COMMIT_TRANSACTION(id_, pattern_, purpose_, handler_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_PREVIEW, .apply_effect = MAELYS_CLI_EFFECT_COMMIT, \
    .output = MAELYS_CLI_OUTPUT_ENVELOPE, .handler = (handler_)
#define MAELYS_CLI_EXECUTE(id_, pattern_, purpose_, handler_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_EXECUTE, .output = MAELYS_CLI_OUTPUT_ENVELOPE, \
    .handler = (handler_)
#define MAELYS_CLI_STREAM(id_, pattern_, purpose_, handler_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_STREAM, .output = MAELYS_CLI_OUTPUT_STREAM, \
    .handler = (handler_)
/* A stream command whose stdio belongs to a named protocol. */
#define MAELYS_CLI_PROTOCOL_STREAM(id_, pattern_, purpose_, handler_, protocol_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_STREAM, .output = MAELYS_CLI_OUTPUT_STREAM, \
    .handler = (handler_), .protocol = (protocol_)
#define MAELYS_CLI_EXTERNAL(id_, pattern_, purpose_, helper_) \
    .id = (id_), .pattern = (pattern_), .purpose = (purpose_), \
    .effect = MAELYS_CLI_EFFECT_EXECUTE, .output = MAELYS_CLI_OUTPUT_STREAM, \
    .delegate = (helper_)

/* Output schema, usually a symbol generated by maelys-cli-embed from a
 * JSON Schema file. */
#define MAELYS_CLI_SCHEMA(symbol_) .output_schema_json = (symbol_)

/* Declares default_text from a numeric constant of the product library,
 * so the default has a single source: {MAELYS_CLI_UNSIGNED(...),
 * MAELYS_CLI_DEFAULT_OF(MY_LIB_MAX_APPROVALS)}. The constant must expand
 * to a plain literal accepted by the option's kind (validated at startup). */
#define MAELYS_CLI_STRINGIFY_(value_) #value_
#define MAELYS_CLI_STRINGIFY(value_) MAELYS_CLI_STRINGIFY_(value_)
#define MAELYS_CLI_DEFAULT_OF(constant_) \
    .default_text = MAELYS_CLI_STRINGIFY(constant_)

/* One contiguous slice of command descriptors, for catalog composition. */
typedef struct maelys_cli_catalog_part {
    const maelys_cli_command_t *commands;
    size_t count;
} maelys_cli_catalog_part_t;
#define MAELYS_CLI_CATALOG_PART(array) {(array), MAELYS_CLI_COUNT(array)}

/* Composes one catalog from several parts, in order, into an array owned
 * by the caller (released with free()). A later part may declare an
 * identifier that an earlier part declares `.unavailable`: it replaces
 * that descriptor in place, so a build variant provides a command where
 * the base catalog describes it as unavailable, at the same position in
 * help and describe. Any other repeated identifier is refused with EEXIST:
 * composition never shadows a provided command silently. Parts with a
 * NULL array or a zero count are skipped. Every other inconsistency is
 * still refused by maelys_cli_catalog_validate() at startup. Returns 0,
 * or -1 with errno EINVAL (NULL output or a part whose array is NULL with
 * a nonzero count), EEXIST, EOVERFLOW or ENOMEM. An empty composition
 * yields a non-NULL array of size zero. */
int maelys_cli_catalog_concat(
    const maelys_cli_catalog_part_t *parts, size_t part_count,
    maelys_cli_command_t **out_commands, size_t *out_count);

const char *maelys_cli_value_kind_name(maelys_cli_value_kind_t kind);

/* Hexadecimal digit count of a digest algorithm name, 0 when unknown. */
size_t maelys_cli_digest_hex_digits(const char *algorithm);
const char *maelys_cli_effect_name(maelys_cli_effect_t effect);
const char *maelys_cli_output_mode_name(maelys_cli_output_mode_t mode);

/* Derives "pattern OPERANDS --required VALUE [--optional VALUE]" from the
 * declaration; required options come first, then optional ones, each in
 * declaration order. Returns -1 when out_size is too small. */
int maelys_cli_command_synopsis(
    const maelys_cli_command_t *command, char *out, size_t out_size);

/* Same, allocated; the caller frees. NULL on allocation failure. */
char *maelys_cli_command_synopsis_alloc(const maelys_cli_command_t *command);

#ifdef __cplusplus
}
#endif

#endif
