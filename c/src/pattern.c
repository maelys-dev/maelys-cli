/* SPDX-License-Identifier: MPL-2.0 */
/* argument.pattern, read and matched here.
 *
 * agent-cli/v2 (section 3, 2.20.0) writes a pattern in a subset that
 * ECMA-262, read with the u flag, and POSIX ERE read the same way, and lets
 * an implementation enforce it "by the means of its choice, a regex engine
 * or code". This is the code: a reader of that
 * subset, which refuses what is outside it, and a matcher that walks the
 * value once with the set of positions the pattern can be at (Thompson,
 * 1968). No regcomp(): the regex of a libc expands a bounded repetition
 * into as many copies of what it repeats, 5.6 MiB and a millisecond for
 * `{0,254}` with the TRE of macOS, paid at every start of every product
 * while the catalog validation compiled each pattern; and each libc reads
 * the forms the subset leaves out its own way (`\d` is a digit for Python
 * and a `d` for TRE). Here a repetition costs its copies of a few
 * instructions, the subset has one reading on every platform, and that
 * reading is held to Python, V8 and RE2 by c/tests/patterns.tsv.
 *
 * The subset, as this file reads it:
 * - a pattern is printable ASCII, U+0020 to U+007E: a libc in the C locale
 *   reads a literal `é` byte by byte, and `é+` repeats its last byte only;
 * - a literal is one of them other than the fourteen metacharacters
 *   `\ ^ $ . [ ] ( ) * + ? { } |`;
 *   `\` before one of the fourteen is that character, before anything else
 *   it is refused (`\d`, `\n`, `\/` are not in the subset);
 * - `.` is any code point but the four line terminators of ECMA-262,
 *   U+000A, U+000D, U+2028 and U+2029;
 * - `[...]` and `[^...]` hold printable ASCII other than `[`, `]` and `\`,
 *   which the two dialects read differently there, and ranges `a-z` with
 *   `a` not above `z`; `-` first or last is itself;
 * - `(...)` groups, `|` alternates, no alternative is empty, `(?` is refused;
 * - `*`, `+`, `?`, `{m}`, `{m,}` and `{m,n}` quantify the literal, `.`,
 *   bracket expression or group before them, once, with m <= n <= 255,
 *   what POSIX guarantees of an ERE engine (RE_DUP_MAX) that would read the
 *   same motif out of `describe`;
 * - `^` and `$` hold at the ends of the value and take no quantifier;
 * - a match is a search: a pattern without anchors matches anywhere.
 * A value is read as UTF-8 code points; a byte that is not UTF-8 is one
 * character, which only `.` and a negated class match, as Python's
 * surrogateescape reads argv. */
#include "maelys/cli/regex.h"
#include "internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Allocation faults: a test build defines MAELYS_CLI_ALLOC_HOOK to a
 * function int hook(void) returning nonzero to fail the allocation about to
 * be made, so that every NO_MEMORY and ENOMEM path is reached. A release
 * build compiles it away. */
#ifdef MAELYS_CLI_ALLOC_HOOK
int MAELYS_CLI_ALLOC_HOOK(void);
static void *hooked_malloc(size_t size) { return MAELYS_CLI_ALLOC_HOOK() ? NULL : malloc(size); }
static void *hooked_calloc(size_t count, size_t size) {
    return MAELYS_CLI_ALLOC_HOOK() ? NULL : calloc(count, size);
}
static void *hooked_realloc(void *old, size_t size) {
    return MAELYS_CLI_ALLOC_HOOK() ? NULL : realloc(old, size);
}
#define malloc hooked_malloc
#define calloc hooked_calloc
#define realloc hooked_realloc
/* The generation a search starts from: near UINT32_MAX, a test reaches the
 * wrap of the counter without a value of 4 GiB. */
extern uint32_t maelys_cli_test_generation;
uint32_t maelys_cli_test_generation;
#endif

/* Copies of a repeated group multiply: the program a pattern expands to is
 * bounded, and a pattern above the bound is refused where the pattern is. */
#define PATTERN_MAX_INSTRUCTIONS MAELYS_CLI_REGEX_MAX_STATES
#define PATTERN_MAX_DEPTH MAELYS_CLI_REGEX_MAX_DEPTH
#define PATTERN_MAX_BOUND MAELYS_CLI_REGEX_MAX_BOUND
#define PATTERN_UNBOUNDED UINT32_MAX
#define PATTERN_NO_TARGET UINT32_MAX

typedef enum {
    NODE_CHAR, NODE_ANY, NODE_CLASS, NODE_BOL, NODE_EOL, NODE_CAT, NODE_ALT,
    NODE_REPEAT
} node_kind_t;

typedef struct node {
    node_kind_t kind;
    uint32_t cp;              /* CHAR */
    uint32_t bits[4];         /* CLASS: one bit per ASCII code point */
    int negated;              /* CLASS */
    uint32_t class_index;     /* CLASS: its place in the compiled classes */
    struct node *child;       /* REPEAT: the repeated */
    struct node **children;   /* CAT, ALT: in order, owned by the node */
    size_t child_count;       /* CAT, ALT: two or more */
    uint32_t min, max;        /* REPEAT */
} node_t;

typedef struct {
    const unsigned char *text;
    size_t length;
    size_t at;
    node_t **nodes;
    size_t count;
    size_t capacity;
    uint32_t classes;         /* class nodes numbered so far */
    maelys_cli_regex_status_t status;
    const char *message;      /* a static sentence, never freed */
    size_t offset;            /* where the reading stopped */
    int failed;
} parser_t;

/* The first refusal is the one reported, at the place reading stopped: the
 * refusals of a pattern are met from left to right. */
static node_t *fail(parser_t *p, maelys_cli_regex_status_t status, const char *message) {
    if (!p->failed) {
        p->status = status;
        p->message = message;
        p->offset = p->at < p->length ? p->at : p->length;
    }
    p->failed = 1;
    return NULL;
}

static node_t *node_new(parser_t *p, node_kind_t kind) {
    if (p->failed) return NULL;
    if (p->count == p->capacity) {
        size_t capacity = p->capacity ? p->capacity * 2u : 16u;
        node_t **grown = realloc(p->nodes, capacity * sizeof(*grown));
        if (!grown) return fail(p, MAELYS_CLI_REGEX_NO_MEMORY, "out of memory");
        p->nodes = grown;
        p->capacity = capacity;
    }
    node_t *node = calloc(1u, sizeof(*node));
    if (!node) return fail(p, MAELYS_CLI_REGEX_NO_MEMORY, "out of memory");
    node->kind = kind;
    p->nodes[p->count++] = node;
    return node;
}

static void nodes_free(parser_t *p) {
    for (size_t i = 0u; i < p->count; ++i) {
        free(p->nodes[i]->children);
        free(p->nodes[i]);
    }
    free(p->nodes);
    p->nodes = NULL;
    p->count = p->capacity = 0u;
}

/* One UTF-8 code point; 0 bytes when the sequence is not UTF-8. */
static size_t utf8_decode(const unsigned char *s, size_t n, uint32_t *cp) {
    if (n == 0u) return 0u;
    unsigned char b = s[0];
    if (b < 0x80u) { *cp = b; return 1u; }
    size_t need;
    uint32_t value;
    uint32_t minimum;
    if (b >= 0xc2u && b <= 0xdfu) { need = 1u; value = b & 0x1fu; minimum = 0x80u; }
    else if (b >= 0xe0u && b <= 0xefu) { need = 2u; value = b & 0x0fu; minimum = 0x800u; }
    else if (b >= 0xf0u && b <= 0xf4u) { need = 3u; value = b & 0x07u; minimum = 0x10000u; }
    else return 0u;
    if (n < need + 1u) return 0u;
    for (size_t i = 1u; i <= need; ++i) {
        if ((s[i] & 0xc0u) != 0x80u) return 0u;
        value = (value << 6) | (s[i] & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
        return 0u;
    *cp = value;
    return need + 1u;
}

static int is_control(uint32_t cp) {
    return cp < 0x20u || (cp >= 0x7fu && cp <= 0x9fu);
}

static int is_meta(uint32_t cp) {
    return cp < 0x80u && strchr("\\^$.[]()*+?{}|", (int)cp) != NULL && cp != 0u;
}

static int at_end(const parser_t *p) { return p->at >= p->length; }
static unsigned char peek(const parser_t *p) { return at_end(p) ? 0u : p->text[p->at]; }

static node_t *parse_alternation(parser_t *p, unsigned depth);

static node_t *parse_class(parser_t *p) {
    /* p->at is after '['. */
    node_t *node = node_new(p, NODE_CLASS);
    if (!node) return NULL;
    node->class_index = p->classes++;
    if (peek(p) == '^') { node->negated = 1; p->at++; }
    size_t items = 0u;
    int after_range = 0;
    for (;;) {
        if (at_end(p)) return fail(p, MAELYS_CLI_REGEX_BRACKET, "an unclosed bracket expression");
        unsigned char c = p->text[p->at];
        /* POSIX reads '-' as itself first, last or as the end of a range
         * only; after a range, ECMA-262 takes it for itself and RE2 for the
         * start of another range: [a-z-9] has no common reading. */
        if (c == '-' && after_range && p->at + 1u < p->length &&
            p->text[p->at + 1u] != ']')
            return fail(p, MAELYS_CLI_REGEX_BRACKET, "a '-' after a range that is not the last character "
                "of the bracket expression, which the two dialects read differently");
        if (c == ']') {
            if (items == 0u)
                return fail(p, MAELYS_CLI_REGEX_BRACKET, "an empty bracket expression, or ']' where the "
                    "two dialects read it differently");
            p->at++;
            return node;
        }
        if (c == '[')
            return fail(p, MAELYS_CLI_REGEX_BRACKET, "'[' inside a bracket expression, as a POSIX class "
                "such as [:alpha:] starts");
        if (c == '\\')
            return fail(p, MAELYS_CLI_REGEX_BRACKET, "'\\' inside a bracket expression, an escape for "
                "ECMA-262 and a character for POSIX ERE");
        if (c < 0x20u || c >= 0x7fu)
            return fail(p, MAELYS_CLI_REGEX_ENCODING, "a character outside printable ASCII in a bracket "
                "expression");
        p->at++;
        uint32_t low = c;
        uint32_t high = c;
        after_range = 0;
        if (peek(p) == '-' && p->at + 1u < p->length && p->text[p->at + 1u] != ']') {
            unsigned char h = p->text[p->at + 1u];
            if (h == '[' || h == '\\')
                return fail(p, MAELYS_CLI_REGEX_BRACKET, "a range that ends on '[' or '\\'");
            if (h < 0x20u || h >= 0x7fu)
                return fail(p, MAELYS_CLI_REGEX_ENCODING, "a character outside printable ASCII in a bracket "
                    "expression");
            if (h < c) return fail(p, MAELYS_CLI_REGEX_BRACKET, "a range whose ends are reversed");
            high = h;
            p->at += 2u;
            after_range = 1;
        }
        for (uint32_t x = low; x <= high; ++x) node->bits[x >> 5] |= 1u << (x & 31u);
        items++;
    }
}

/* {m}, {m,} or {m,n}; p->at is after '{'. */
static int parse_bound(parser_t *p, uint32_t *min, uint32_t *max) {
    uint32_t values[2] = {0u, 0u};
    int digits[2] = {0, 0};
    size_t part = 0u;
    for (;;) {
        if (at_end(p)) { fail(p, MAELYS_CLI_REGEX_BOUND, "an unclosed bound: write {m}, {m,} or {m,n}"); return -1; }
        unsigned char c = p->text[p->at++];
        if (c >= '0' && c <= '9') {
            /* A number is written in its decimal form: RE2 refuses {0,054},
             * ECMA-262 reads it as 54. */
            if (digits[part] == 1 && values[part] == 0u) {
                fail(p, MAELYS_CLI_REGEX_BOUND, "a bound written with a leading zero");
                return -1;
            }
            if (values[part] > PATTERN_MAX_BOUND) continue; /* already too large */
            values[part] = values[part] * 10u + (uint32_t)(c - '0');
            digits[part]++;
            continue;
        }
        if (c == ',' && part == 0u) { part = 1u; continue; }
        if (c == '}') break;
        fail(p, MAELYS_CLI_REGEX_BOUND, "'{' that does not open a bound {m}, {m,} or {m,n}");
        return -1;
    }
    if (!digits[0]) { fail(p, MAELYS_CLI_REGEX_BOUND, "a bound without its minimum: write {m}, {m,} or {m,n}"); return -1; }
    *min = values[0];
    *max = part == 0u ? values[0] : digits[1] ? values[1] : PATTERN_UNBOUNDED;
    if (*min > PATTERN_MAX_BOUND || (*max != PATTERN_UNBOUNDED && *max > PATTERN_MAX_BOUND)) {
        fail(p, MAELYS_CLI_REGEX_LIMIT, "a bound above 255, more than POSIX guarantees of an ERE engine");
        return -1;
    }
    if (*max != PATTERN_UNBOUNDED && *min > *max) { fail(p, MAELYS_CLI_REGEX_BOUND, "a bound {m,n} with m above n"); return -1; }
    return 0;
}

static node_t *parse_atom(parser_t *p, unsigned depth) {
    unsigned char c = peek(p);
    if (c == '(') {
        p->at++;
        if (peek(p) == '?')
            return fail(p, MAELYS_CLI_REGEX_GROUP, "'(?', a group ECMA-262 names and POSIX ERE lacks");
        if (depth + 1u > PATTERN_MAX_DEPTH) return fail(p, MAELYS_CLI_REGEX_LIMIT, "more than 32 nested groups");
        node_t *inner = parse_alternation(p, depth + 1u);
        if (!inner) return NULL;
        if (peek(p) != ')') return fail(p, MAELYS_CLI_REGEX_GROUP, "an unclosed group");
        p->at++;
        return inner;
    }
    if (c == '[') { p->at++; return parse_class(p); }
    if (c == '.') { p->at++; return node_new(p, NODE_ANY); }
    if (c == '^') { p->at++; return node_new(p, NODE_BOL); }
    if (c == '$') { p->at++; return node_new(p, NODE_EOL); }
    if (c == ')') return fail(p, MAELYS_CLI_REGEX_GROUP, "a ')' that closes no group");
    if (c == ']' || c == '}')
        return fail(p, MAELYS_CLI_REGEX_GROUP, "an unescaped ']' or '}', which the two dialects read "
            "differently: write \\] or \\}");
    if (c == '*' || c == '+' || c == '?' || c == '{')
        return fail(p, MAELYS_CLI_REGEX_QUANTIFIER, "a quantifier that follows nothing");
    uint32_t cp;
    if (c == '\\') {
        p->at++;
        if (at_end(p)) return fail(p, MAELYS_CLI_REGEX_ESCAPE, "a '\\' that ends the pattern");
        unsigned char e = p->text[p->at];
        if (!is_meta(e))
            return fail(p, MAELYS_CLI_REGEX_ESCAPE, "an escape that is not of a metacharacter: '\\d', "
                "'\\n' and the like are not in the subset");
        p->at++;
        cp = e;
    } else {
        if (is_control(c)) return fail(p, MAELYS_CLI_REGEX_ENCODING, "a control character");
        if (c >= 0x80u)
            return fail(p, MAELYS_CLI_REGEX_ENCODING, "a character outside printable ASCII, which a libc "
                "in the C locale reads byte by byte");
        p->at++;
        cp = c;
    }
    node_t *node = node_new(p, NODE_CHAR);
    if (node) node->cp = cp;
    return node;
}

static node_t *parse_piece(parser_t *p, unsigned depth) {
    node_t *atom = parse_atom(p, depth);
    if (!atom) return NULL;
    unsigned char c = peek(p);
    if (c != '*' && c != '+' && c != '?' && c != '{') return atom;
    if (atom->kind == NODE_BOL || atom->kind == NODE_EOL)
        return fail(p, MAELYS_CLI_REGEX_QUANTIFIER, "a quantified anchor");
    uint32_t min, max;
    p->at++;
    if (c == '*') { min = 0u; max = PATTERN_UNBOUNDED; }
    else if (c == '+') { min = 1u; max = PATTERN_UNBOUNDED; }
    else if (c == '?') { min = 0u; max = 1u; }
    else if (parse_bound(p, &min, &max) != 0) return NULL;
    c = peek(p);
    if (c == '*' || c == '+' || c == '?' || c == '{')
        return fail(p, MAELYS_CLI_REGEX_QUANTIFIER, "two quantifiers in a row, or a lazy quantifier such as "
            "'*?', which POSIX ERE lacks");
    node_t *repeat = node_new(p, NODE_REPEAT);
    if (!repeat) return NULL;
    repeat->child = atom;
    repeat->min = min;
    repeat->max = max;
    return repeat;
}

/* A sequence or an alternation is one node holding its items in order: the
 * depth of the tree, and of every recursion over it (node_size, emit_node,
 * the parser itself), is bounded by the nesting of groups,
 * PATTERN_MAX_DEPTH, never by the length of the pattern. */
typedef struct {
    node_t **items;
    size_t count;
    size_t capacity;
} items_t;

static int items_push(parser_t *p, items_t *list, node_t *item) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2u : 4u;
        node_t **grown = realloc(list->items, capacity * sizeof(*grown));
        if (!grown) { fail(p, MAELYS_CLI_REGEX_NO_MEMORY, "out of memory"); return -1; }
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = item;
    return 0;
}

/* The single item itself, or a node of the kind that owns the list. */
static node_t *items_join(parser_t *p, items_t *list, node_kind_t kind) {
    if (list->count == 1u) {
        node_t *only = list->items[0];
        free(list->items);
        return only;
    }
    node_t *node = node_new(p, kind);
    if (!node) { free(list->items); return NULL; }
    node->children = list->items;
    node->child_count = list->count;
    return node;
}

static node_t *parse_sequence(parser_t *p, unsigned depth) {
    items_t pieces = {NULL, 0u, 0u};
    while (!at_end(p) && peek(p) != '|' && peek(p) != ')') {
        node_t *piece = parse_piece(p, depth);
        if (!piece || items_push(p, &pieces, piece) != 0) {
            free(pieces.items);
            return NULL;
        }
    }
    if (pieces.count == 0u) return fail(p, MAELYS_CLI_REGEX_EMPTY, "an empty alternative or group");
    return items_join(p, &pieces, NODE_CAT);
}

static node_t *parse_alternation(parser_t *p, unsigned depth) {
    items_t alternatives = {NULL, 0u, 0u};
    for (;;) {
        node_t *alternative = parse_sequence(p, depth);
        if (!alternative || items_push(p, &alternatives, alternative) != 0) {
            free(alternatives.items);
            return NULL;
        }
        if (peek(p) != '|') break;
        p->at++;
    }
    return items_join(p, &alternatives, NODE_ALT);
}

/* ---- the program ------------------------------------------------------- */

typedef enum {
    OP_CHAR, OP_ANY, OP_CLASS, OP_SPLIT, OP_JMP, OP_BOL, OP_EOL, OP_MATCH
} op_t;

typedef struct {
    uint8_t op;
    uint32_t x;               /* CHAR: the code point; SPLIT/JMP: a target;
                                 CLASS: its index in the classes */
    uint32_t y;               /* SPLIT: the other target; CLASS: negated */
} instruction_t;

/* A compiled pattern: its program and the classes the program reads, owned
 * by the handle and never written after compile, so that searches share it
 * across threads. */
struct maelys_cli_regex {
    instruction_t *code;
    uint32_t count;
    uint32_t (*classes)[4];
    uint32_t class_count;
};

/* Instructions a node emits, saturating above the bound. */
static uint32_t node_size(const node_t *node) {
    const uint32_t cap = PATTERN_MAX_INSTRUCTIONS + 1u;
    switch (node->kind) {
    case NODE_CAT:
    case NODE_ALT: {
        /* An alternation of k items adds k - 1 splits and k - 1 jumps. */
        uint64_t total = node->kind == NODE_ALT ? 2u * (uint64_t)(node->child_count - 1u) : 0u;
        for (size_t i = 0u; i < node->child_count && total <= cap; ++i)
            total += node_size(node->children[i]);
        return total > cap ? cap : (uint32_t)total;
    }
    case NODE_REPEAT: {
        uint64_t c = node_size(node->child);
        uint64_t total;
        if (node->max == PATTERN_UNBOUNDED) total = (uint64_t)node->min * c + c + 2u;
        else total = (uint64_t)node->min * c + (uint64_t)(node->max - node->min) * (c + 1u);
        return total > cap ? cap : (uint32_t)total;
    }
    default:
        return 1u;
    }
}

typedef struct {
    instruction_t *code;
    uint32_t count;
} emitter_t;

static uint32_t emit(emitter_t *e, op_t op, uint32_t x, uint32_t y) {
    instruction_t *at = &e->code[e->count];
    at->op = (uint8_t)op;
    at->x = x;
    at->y = y;
    return e->count++;
}

static void emit_node(emitter_t *e, const node_t *node) {
    switch (node->kind) {
    case NODE_CHAR: emit(e, OP_CHAR, node->cp, 0u); return;
    case NODE_ANY: emit(e, OP_ANY, 0u, 0u); return;
    case NODE_BOL: emit(e, OP_BOL, 0u, 0u); return;
    case NODE_EOL: emit(e, OP_EOL, 0u, 0u); return;
    case NODE_CLASS: emit(e, OP_CLASS, node->class_index, (uint32_t)node->negated); return;
    case NODE_CAT:
        for (size_t i = 0u; i < node->child_count; ++i) emit_node(e, node->children[i]);
        return;
    case NODE_ALT: {
        /* split L1, M1; L1: a; jmp END; M1: split L2, M2; ...; z; END:
         * The jumps to END are chained through their x until END is known:
         * emission allocates nothing. */
        uint32_t chain = PATTERN_NO_TARGET;
        for (size_t i = 0u; i + 1u < node->child_count; ++i) {
            uint32_t split = emit(e, OP_SPLIT, 0u, 0u);
            e->code[split].x = e->count;
            emit_node(e, node->children[i]);
            chain = emit(e, OP_JMP, chain, 0u);
            e->code[split].y = e->count;
        }
        emit_node(e, node->children[node->child_count - 1u]);
        while (chain != PATTERN_NO_TARGET) {
            uint32_t next = e->code[chain].x;
            e->code[chain].x = e->count;
            chain = next;
        }
        return;
    }
    case NODE_REPEAT: {
        for (uint32_t i = 0u; i < node->min; ++i) emit_node(e, node->child);
        if (node->max == PATTERN_UNBOUNDED) {
            /* L1: split L2, L3; L2: e; jmp L1; L3: */
            uint32_t split = emit(e, OP_SPLIT, 0u, 0u);
            e->code[split].x = e->count;
            emit_node(e, node->child);
            emit(e, OP_JMP, split, 0u);
            e->code[split].y = e->count;
            return;
        }
        /* (max - min) optional copies, each split skipping to the end; the
         * splits are chained through their y until the end is known. */
        uint32_t chain = PATTERN_NO_TARGET;
        for (uint32_t i = 0u; i < node->max - node->min; ++i) {
            chain = emit(e, OP_SPLIT, 0u, chain);
            e->code[chain].x = e->count;
            emit_node(e, node->child);
        }
        while (chain != PATTERN_NO_TARGET) {
            uint32_t next = e->code[chain].y;
            e->code[chain].y = e->count;
            chain = next;
        }
        return;
    }
    }
}

/* ---- the match --------------------------------------------------------- */

typedef struct {
    const instruction_t *code;
    const uint32_t (*classes)[4];
    uint32_t count;
    unsigned char *list[2];   /* the positions alive before and after a step */
    uint32_t size[2];
    unsigned char *mark;      /* generation a position was last added in */
    unsigned char *stack;
    uint32_t generation;
    int matched;
    uint64_t steps;           /* positions popped and threads tried: the work */
} machine_t;

/* The working memory is the caller's storage of any type, a declared array
 * of char included (maelys_cli_regex_search_scratch): its words are read
 * and written through memcpy, never through a uint32_t lvalue, which the
 * effective type of such storage forbids. */
static inline uint32_t word_get(const unsigned char *base, size_t index) {
    uint32_t value;
    memcpy(&value, base + index * sizeof(value), sizeof(value));
    return value;
}

static inline void word_put(unsigned char *base, size_t index, uint32_t value) {
    memcpy(base + index * sizeof(value), &value, sizeof(value));
}

/* Follows the empty moves from pc and records every position that consumes
 * a character, or the match. Iterative: a chain of splits can be as long
 * as the program, and the stack holds count + 1 entries (see
 * maelys_cli_pattern_match). Inline: called in the loop of every step, which
 * the compilers stop inlining by themselves once its words are memcpy. */
static inline void add_thread(machine_t *m, int which, uint32_t pc, int at_start, int at_last) {
    uint32_t top = 0u;
    word_put(m->stack, top++, pc);
    while (top) {
        pc = word_get(m->stack, --top);
        m->steps++;
        if (word_get(m->mark, pc) == m->generation) continue;
        word_put(m->mark, pc, m->generation);
        const instruction_t *i = &m->code[pc];
        switch ((op_t)i->op) {
        case OP_JMP: word_put(m->stack, top++, i->x); break;
        case OP_SPLIT:
            word_put(m->stack, top++, i->y);
            word_put(m->stack, top++, i->x);
            break;
        case OP_BOL: if (at_start) word_put(m->stack, top++, pc + 1u); break;
        case OP_EOL: if (at_last) word_put(m->stack, top++, pc + 1u); break;
        case OP_MATCH: m->matched = 1; break;
        default: word_put(m->list[which], m->size[which]++, pc); break;
        }
    }
}

static int instruction_accepts(const machine_t *m, const instruction_t *i, uint32_t cp) {
    switch ((op_t)i->op) {
    case OP_CHAR: return i->x == cp;
    case OP_ANY:
        /* The four line terminators of ECMA-262 (agent-cli/v2, section 3). */
        return cp != 0x0Au && cp != 0x0Du && cp != 0x2028u && cp != 0x2029u;
    case OP_CLASS: {
        int in = cp < 0x80u && (m->classes[i->x][cp >> 5] & (1u << (cp & 31u))) != 0u;
        return i->y ? !in : in;
    }
    default: return 0;
    }
}

/* One code point of the value, a byte that is not UTF-8 reading as one
 * character that no literal and no class names. */
static size_t value_next(const unsigned char *s, size_t n, uint32_t *cp) {
    size_t used = utf8_decode(s, n, cp);
    if (used) return used;
    *cp = 0xdc00u | s[0];
    return 1u;
}

/* The next generation. Where the counter wraps, after 2^32 - 1 code points,
 * it starts over at 1 with the marks cleared: 0 is the mark of no
 * generation, and a mark left by an earlier one would hide a position. */
static void next_generation(machine_t *m) {
    if (++m->generation == 0u) {
        memset(m->mark, 0, (size_t)m->count * sizeof(uint32_t));
        m->generation = 1u;
    }
}

static int run(machine_t *m, const char *value, size_t n) {
    const unsigned char *s = (const unsigned char *)value;
    size_t at = 0u;
    int which = 0;
    next_generation(m);
    m->size[0] = m->size[1] = 0u;
    add_thread(m, which, 0u, 1, n == 0u);
    while (!m->matched && at < n) {
        uint32_t cp;
        size_t used = value_next(s + at, n - at, &cp);
        at += used;
        int next = which ^ 1;
        m->size[next] = 0u;
        next_generation(m);
        for (uint32_t t = 0u; t < m->size[which] && !m->matched; ++t) {
            uint32_t pc = word_get(m->list[which], t);
            m->steps++;
            if (instruction_accepts(m, &m->code[pc], cp))
                add_thread(m, next, pc + 1u, 0, at == n);
        }
        /* A search: the pattern may also start here. */
        if (!m->matched) add_thread(m, next, 0u, 0, at == n);
        which = next;
    }
    return m->matched;
}

/* ---- the public entry points (maelys/cli/regex.h) -------------------- */

static node_t *parse(parser_t *p, const char *pattern, size_t length) {
    memset(p, 0, sizeof(*p));
    p->text = (const unsigned char *)pattern;
    p->length = length;
    if (length == 0u) return fail(p, MAELYS_CLI_REGEX_EMPTY, "an empty pattern");
    node_t *root = parse_alternation(p, 0u);
    if (root && !at_end(p)) root = fail(p, MAELYS_CLI_REGEX_GROUP, "a ')' that closes no group");
    /* The state bound is met once the whole pattern is read: a refusal of
     * reading, met before, is the one reported. */
    if (root && node_size(root) + 1u > PATTERN_MAX_INSTRUCTIONS) {
        p->at = p->length;
        root = fail(p, MAELYS_CLI_REGEX_LIMIT, "a pattern that expands to more than 65536 states");
    }
    return p->failed ? NULL : root;
}

static void diagnose(maelys_cli_regex_diagnostic_t *diagnostic,
                     maelys_cli_regex_status_t status, size_t offset, const char *message) {
    if (!diagnostic) return;
    diagnostic->status = status;
    diagnostic->offset = offset;
    diagnostic->message = message;
}

maelys_cli_regex_status_t maelys_cli_regex_compile(
    const char *pattern, size_t length, maelys_cli_regex_t **out,
    maelys_cli_regex_diagnostic_t *diagnostic) {
    if (out) *out = NULL;
    if (!out || (!pattern && length > 0u)) {
        diagnose(diagnostic, MAELYS_CLI_REGEX_INVALID_ARGUMENT, 0u,
            "an argument the function does not accept: out is NULL, or the "
            "pattern is NULL with a length");
        return MAELYS_CLI_REGEX_INVALID_ARGUMENT;
    }
    parser_t p;
    node_t *root = parse(&p, pattern, length);
    if (!root) {
        maelys_cli_regex_status_t status = p.status;
        size_t offset = status == MAELYS_CLI_REGEX_NO_MEMORY ? 0u : p.offset;
        diagnose(diagnostic, status, offset, p.message);
        nodes_free(&p);
        return status;
    }
    uint32_t count = node_size(root) + 1u;
    maelys_cli_regex_t *regex = calloc(1u, sizeof(*regex));
    instruction_t *code = regex ? malloc(count * sizeof(*code)) : NULL;
    uint32_t (*classes)[4] = regex && code && p.classes ?
        calloc(p.classes, sizeof(*classes)) : NULL;
    if (!regex || !code || (p.classes && !classes)) {
        free(classes);
        free(code);
        free(regex);
        nodes_free(&p);
        diagnose(diagnostic, MAELYS_CLI_REGEX_NO_MEMORY, 0u, "out of memory");
        return MAELYS_CLI_REGEX_NO_MEMORY;
    }
    /* A pattern without a class has no class array, and no class node. */
    for (size_t i = 0u; classes && i < p.count; ++i)
        if (p.nodes[i]->kind == NODE_CLASS)
            memcpy(classes[p.nodes[i]->class_index], p.nodes[i]->bits, sizeof(classes[0]));
    emitter_t e = {code, 0u};
    emit_node(&e, root);
    emit(&e, OP_MATCH, 0u, 0u);
    nodes_free(&p);
    regex->code = code;
    regex->count = e.count;
    regex->classes = classes;
    regex->class_count = p.classes;
    *out = regex;
    diagnose(diagnostic, MAELYS_CLI_REGEX_OK, 0u, NULL);
    return MAELYS_CLI_REGEX_OK;
}

/* The working memory of a search of a program of `count` positions: two
 * lists of positions, the marks, and the stack of add_thread, one word
 * each. Only a split deepens the stack (one popped, two pushed), a jump
 * keeps its depth and every other position shortens it, and within one
 * call each position is expanded once: count + 1 entries, the first push
 * included. Three more bytes let the words start on a multiple of 4
 * wherever the scratch starts: alignment is for speed only, every access
 * being a memcpy. At most about 1 MiB (MAELYS_CLI_REGEX_MAX_STATES). */
#define SCRATCH_ALIGNMENT sizeof(uint32_t)

static size_t scratch_bytes(uint32_t count) {
    return ((size_t)count * 4u + 1u) * sizeof(uint32_t) + SCRATCH_ALIGNMENT - 1u;
}

/* A search in `scratch`, of at least scratch_bytes(regex->count) bytes,
 * whose content is not read. The arguments are valid; never allocates. */
static int search_in(const maelys_cli_regex_t *regex, void *scratch,
                     const char *value, size_t length, uint64_t *steps) {
    uint32_t count = regex->count;
    unsigned char *base = scratch;
    size_t skew = (size_t)((uintptr_t)base % SCRATCH_ALIGNMENT);
    if (skew) base += SCRATCH_ALIGNMENT - skew;
    size_t words = (size_t)count * sizeof(uint32_t);
    machine_t m;
    memset(&m, 0, sizeof(m));
    m.code = regex->code;
    m.classes = (const uint32_t (*)[4])regex->classes;
    m.count = count;
    m.list[0] = base;
    m.list[1] = base + words;
    m.mark = base + 2u * words;
    m.stack = base + 3u * words;
    memset(m.mark, 0, words);
#ifdef MAELYS_CLI_ALLOC_HOOK
    m.generation = maelys_cli_test_generation;
#endif
    int result = run(&m, value ? value : "", length);
    if (steps) *steps = m.steps;
    return result;
}

static int search_counted(const maelys_cli_regex_t *regex, const char *value,
                          size_t length, uint64_t *steps) {
    if (steps) *steps = 0u;
    if (!regex || (!value && length > 0u)) {
        errno = EINVAL;
        return -1;
    }
    void *scratch = malloc(scratch_bytes(regex->count));
    if (!scratch) {
        errno = ENOMEM;
        return -1;
    }
    int result = search_in(regex, scratch, value, length, steps);
    free(scratch);
    return result;
}

int maelys_cli_regex_search(const maelys_cli_regex_t *regex, const char *value, size_t length) {
    return search_counted(regex, value, length, NULL);
}

size_t maelys_cli_regex_scratch_size(const maelys_cli_regex_t *regex) {
    return regex ? scratch_bytes(regex->count) : 0u;
}

int maelys_cli_regex_search_scratch(
    const maelys_cli_regex_t *regex, void *scratch, size_t scratch_size,
    const char *value, size_t length) {
    if (!regex || !scratch || (!value && length > 0u) ||
        scratch_size < scratch_bytes(regex->count)) {
        errno = EINVAL;
        return -1;
    }
    return search_in(regex, scratch, value, length, NULL);
}

size_t maelys_cli_regex_states(const maelys_cli_regex_t *regex) {
    return regex ? regex->count : 0u;
}

void maelys_cli_regex_free(maelys_cli_regex_t *regex) {
    if (!regex) return;
    free(regex->classes);
    free(regex->code);
    free(regex);
}

const char *maelys_cli_regex_status_text(maelys_cli_regex_status_t status) {
    switch (status) {
    case MAELYS_CLI_REGEX_OK: return "the pattern is in the subset";
    case MAELYS_CLI_REGEX_INVALID_ARGUMENT: return "an argument the function does not accept";
    case MAELYS_CLI_REGEX_NO_MEMORY: return "out of memory";
    case MAELYS_CLI_REGEX_EMPTY: return "an empty pattern, alternative or group";
    case MAELYS_CLI_REGEX_ENCODING: return "a character outside printable ASCII";
    case MAELYS_CLI_REGEX_ESCAPE: return "an escape that is not of a metacharacter";
    case MAELYS_CLI_REGEX_BRACKET: return "a bracket expression outside the subset";
    case MAELYS_CLI_REGEX_GROUP: return "a group, a ')', a ']' or a '}' outside the subset";
    case MAELYS_CLI_REGEX_QUANTIFIER: return "a quantifier outside the subset";
    case MAELYS_CLI_REGEX_BOUND: return "a bound that is not {m}, {m,} or {m,n} in decimal";
    case MAELYS_CLI_REGEX_LIMIT:
        return "a bound above 255, more than 32 nested groups or more than 65536 states";
    }
    return "an unknown status";
}

/* ---- the reader of argument.pattern (internal.h) ----------------------- */

int maelys_cli_pattern_check(const char *pattern, char *reason, size_t reason_size) {
    maelys_cli_regex_t *regex = NULL;
    maelys_cli_regex_diagnostic_t diagnostic;
    maelys_cli_regex_status_t status = maelys_cli_regex_compile(
        pattern ? pattern : "", pattern ? strlen(pattern) : 0u, &regex, &diagnostic);
    maelys_cli_regex_free(regex);
    if (reason && reason_size)
        (void)snprintf(reason, reason_size, "%s",
            status == MAELYS_CLI_REGEX_OK ? "" : diagnostic.message);
    return status == MAELYS_CLI_REGEX_OK ? 0 : -1;
}

int maelys_cli_pattern_match(const char *pattern, const char *value) {
    return maelys_cli_pattern_match_counted(pattern, value, NULL, NULL);
}

int maelys_cli_pattern_match_counted(const char *pattern, const char *value,
                                     uint64_t *steps, uint32_t *states) {
    if (steps) *steps = 0u;
    if (states) *states = 0u;
    if (!value) return 0;
    maelys_cli_regex_t *regex = NULL;
    if (maelys_cli_regex_compile(pattern ? pattern : "", pattern ? strlen(pattern) : 0u,
            &regex, NULL) != MAELYS_CLI_REGEX_OK)
        return -1;
    int result = search_counted(regex, value, strlen(value), steps);
    if (states) *states = regex->count;
    maelys_cli_regex_free(regex);
    return result;
}
