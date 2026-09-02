#include "maelys/cli/json.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- UTF-8 ------------------------------------------------------------- */

/* Returns the byte length of the well-formed UTF-8 sequence starting at
 * text (1..4), or 0 for an invalid or truncated sequence, an overlong
 * encoding, a surrogate or a code point above U+10FFFF. */
static size_t utf8_sequence(const unsigned char *text, size_t remaining) {
    unsigned char lead = text[0];
    if (lead < 0x80u) return 1u;
    size_t length;
    uint32_t minimum;
    uint32_t code;
    if ((lead & 0xE0u) == 0xC0u) { length = 2u; minimum = 0x80u; code = lead & 0x1Fu; }
    else if ((lead & 0xF0u) == 0xE0u) { length = 3u; minimum = 0x800u; code = lead & 0x0Fu; }
    else if ((lead & 0xF8u) == 0xF0u) { length = 4u; minimum = 0x10000u; code = lead & 0x07u; }
    else return 0u;
    if (remaining < length) return 0u;
    for (size_t i = 1u; i < length; ++i) {
        if ((text[i] & 0xC0u) != 0x80u) return 0u;
        code = (code << 6) | (text[i] & 0x3Fu);
    }
    if (code < minimum || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
        return 0u;
    return length;
}

/* ---- writer ---------------------------------------------------------- */

void maelys_cli_json_writer_init(maelys_cli_json_writer_t *writer) {
    if (writer) memset(writer, 0, sizeof(*writer));
}

void maelys_cli_json_writer_clear(maelys_cli_json_writer_t *writer) {
    if (!writer) return;
    free(writer->data);
    memset(writer, 0, sizeof(*writer));
}

static int reserve(maelys_cli_json_writer_t *writer, size_t extra) {
    if (writer->failed) return -1;
    if (extra > SIZE_MAX - writer->size - 1u) {
        writer->failed = 1;
        return -1;
    }
    size_t needed = writer->size + extra + 1u;
    if (needed <= writer->capacity) return 0;
    size_t capacity = writer->capacity ? writer->capacity : 256u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            writer->failed = 1;
            return -1;
        }
        capacity *= 2u;
    }
    char *grown = realloc(writer->data, capacity);
    if (!grown) {
        writer->failed = 1;
        return -1;
    }
    writer->data = grown;
    writer->capacity = capacity;
    return 0;
}

static int append(maelys_cli_json_writer_t *writer, const char *text,
    size_t length) {
    if (reserve(writer, length) != 0) return -1;
    memcpy(writer->data + writer->size, text, length);
    writer->size += length;
    writer->data[writer->size] = '\0';
    return 0;
}

static int append_char(maelys_cli_json_writer_t *writer, char c) {
    return append(writer, &c, 1u);
}

static int append_escaped(maelys_cli_json_writer_t *writer,
    const char *value, size_t length) {
    if (append_char(writer, '"') != 0) return -1;
    for (size_t i = 0u; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c >= 0x80u) {
            size_t sequence = utf8_sequence((const unsigned char *)value + i,
                length - i);
            if (sequence == 0u) {
                /* Invalid UTF-8 is a caller defect: JSON cannot carry it. */
                writer->failed = 1;
                return -1;
            }
            if (append(writer, value + i, sequence) != 0) return -1;
            i += sequence - 1u;
            continue;
        }
        const char *replacement = NULL;
        switch (c) {
            case '"': replacement = "\\\""; break;
            case '\\': replacement = "\\\\"; break;
            case '\n': replacement = "\\n"; break;
            case '\r': replacement = "\\r"; break;
            case '\t': replacement = "\\t"; break;
            case '\b': replacement = "\\b"; break;
            case '\f': replacement = "\\f"; break;
            default: break;
        }
        if (replacement) {
            if (append(writer, replacement, strlen(replacement)) != 0)
                return -1;
        } else if (c < 0x20u) {
            char buffer[8];
            (void)snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned)c);
            if (append(writer, buffer, 6u) != 0) return -1;
        } else if (append_char(writer, (char)c) != 0) {
            return -1;
        }
    }
    return append_char(writer, '"');
}

static int begin_value(maelys_cli_json_writer_t *writer) {
    if (writer->failed) return -1;
    if (writer->pending_key) {
        writer->pending_key = 0;
        return 0;
    }
    if (writer->depth > 0u) {
        if (writer->is_object[writer->depth - 1u]) {
            /* A value inside an object must follow a key. */
            writer->failed = 1;
            return -1;
        }
        if (writer->has_items[writer->depth - 1u] &&
            append_char(writer, ',') != 0)
            return -1;
        writer->has_items[writer->depth - 1u] = 1u;
    } else if (writer->size > 0u) {
        /* A second top-level value is never valid. */
        writer->failed = 1;
        return -1;
    }
    return 0;
}

static int open_container(maelys_cli_json_writer_t *writer, char bracket) {
    if (begin_value(writer) != 0) return -1;
    if (writer->depth >= MAELYS_CLI_JSON_MAX_DEPTH) {
        writer->failed = 1;
        return -1;
    }
    writer->has_items[writer->depth] = 0u;
    writer->is_object[writer->depth] = bracket == '{';
    writer->depth++;
    return append_char(writer, bracket);
}

static int close_container(maelys_cli_json_writer_t *writer, char bracket) {
    if (writer->failed || writer->depth == 0u || writer->pending_key) {
        writer->failed = 1;
        return -1;
    }
    --writer->depth;
    return append_char(writer, bracket);
}

int maelys_cli_json_begin_object(maelys_cli_json_writer_t *writer) {
    return writer ? open_container(writer, '{') : -1;
}

int maelys_cli_json_end_object(maelys_cli_json_writer_t *writer) {
    return writer ? close_container(writer, '}') : -1;
}

int maelys_cli_json_begin_array(maelys_cli_json_writer_t *writer) {
    return writer ? open_container(writer, '[') : -1;
}

int maelys_cli_json_end_array(maelys_cli_json_writer_t *writer) {
    return writer ? close_container(writer, ']') : -1;
}

int maelys_cli_json_key(maelys_cli_json_writer_t *writer, const char *key) {
    if (!writer || !key || writer->depth == 0u || writer->pending_key ||
        !writer->is_object[writer->depth - 1u]) {
        if (writer) writer->failed = 1;
        return -1;
    }
    if (writer->has_items[writer->depth - 1u] && append_char(writer, ',') != 0)
        return -1;
    writer->has_items[writer->depth - 1u] = 1u;
    if (append_escaped(writer, key, strlen(key)) != 0 ||
        append_char(writer, ':') != 0)
        return -1;
    writer->pending_key = 1;
    return 0;
}

int maelys_cli_json_stringn(
    maelys_cli_json_writer_t *writer, const char *value, size_t length) {
    if (!writer || !value) return -1;
    if (begin_value(writer) != 0) return -1;
    return append_escaped(writer, value, length);
}

int maelys_cli_json_string(maelys_cli_json_writer_t *writer, const char *value) {
    if (!value) {
        /* A missing string is a caller defect, never a silent null. */
        if (writer) writer->failed = 1;
        return -1;
    }
    return maelys_cli_json_stringn(writer, value, strlen(value));
}

int maelys_cli_json_integer(maelys_cli_json_writer_t *writer, int64_t value) {
    if (!writer || begin_value(writer) != 0) return -1;
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    return written > 0 ? append(writer, buffer, (size_t)written) : -1;
}

int maelys_cli_json_unsigned(maelys_cli_json_writer_t *writer, uint64_t value) {
    if (!writer || begin_value(writer) != 0) return -1;
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    return written > 0 ? append(writer, buffer, (size_t)written) : -1;
}

int maelys_cli_json_boolean(maelys_cli_json_writer_t *writer, int value) {
    if (!writer || begin_value(writer) != 0) return -1;
    return value ? append(writer, "true", 4u) : append(writer, "false", 5u);
}

int maelys_cli_json_null(maelys_cli_json_writer_t *writer) {
    if (!writer || begin_value(writer) != 0) return -1;
    return append(writer, "null", 4u);
}

int maelys_cli_json_raw(maelys_cli_json_writer_t *writer, const char *json) {
    if (!writer || !json) return -1;
    size_t length = strlen(json);
    if (maelys_cli_json_validate(json, length, NULL) != 0) {
        writer->failed = 1;
        return -1;
    }
    if (begin_value(writer) != 0) return -1;
    /* Trim surrounding whitespace so compact output stays compact. */
    size_t start = 0u;
    while (start < length && (json[start] == ' ' || json[start] == '\n' ||
           json[start] == '\r' || json[start] == '\t'))
        ++start;
    size_t end = length;
    while (end > start && (json[end - 1u] == ' ' || json[end - 1u] == '\n' ||
           json[end - 1u] == '\r' || json[end - 1u] == '\t'))
        --end;
    return append(writer, json + start, end - start);
}

int maelys_cli_json_key_string(
    maelys_cli_json_writer_t *writer, const char *key, const char *value) {
    return maelys_cli_json_key(writer, key) == 0 ?
        maelys_cli_json_string(writer, value) : -1;
}

int maelys_cli_json_key_integer(
    maelys_cli_json_writer_t *writer, const char *key, int64_t value) {
    return maelys_cli_json_key(writer, key) == 0 ?
        maelys_cli_json_integer(writer, value) : -1;
}

int maelys_cli_json_key_unsigned(
    maelys_cli_json_writer_t *writer, const char *key, uint64_t value) {
    return maelys_cli_json_key(writer, key) == 0 ?
        maelys_cli_json_unsigned(writer, value) : -1;
}

int maelys_cli_json_key_boolean(
    maelys_cli_json_writer_t *writer, const char *key, int value) {
    return maelys_cli_json_key(writer, key) == 0 ?
        maelys_cli_json_boolean(writer, value) : -1;
}

int maelys_cli_json_key_raw(
    maelys_cli_json_writer_t *writer, const char *key, const char *json) {
    return maelys_cli_json_key(writer, key) == 0 ?
        maelys_cli_json_raw(writer, json) : -1;
}

char *maelys_cli_json_finish(maelys_cli_json_writer_t *writer) {
    if (!writer) return NULL;
    if (writer->failed || writer->depth != 0u || writer->pending_key ||
        writer->size == 0u) {
        maelys_cli_json_writer_clear(writer);
        return NULL;
    }
    char *text = writer->data;
    memset(writer, 0, sizeof(*writer));
    return text;
}

/* ---- scanner --------------------------------------------------------- */

typedef struct scanner {
    const char *text;
    size_t length;
    size_t offset;
    size_t depth;
    maelys_cli_json_writer_t *out; /* NULL when only validating */
    int compact;
} scanner_t;

static int is_space(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static void skip_space(scanner_t *scanner) {
    while (scanner->offset < scanner->length &&
           is_space(scanner->text[scanner->offset]))
        ++scanner->offset;
}

static int emit(scanner_t *scanner, const char *text, size_t length) {
    return scanner->out ? append(scanner->out, text, length) : 0;
}

static int emit_newline(scanner_t *scanner) {
    if (!scanner->out || scanner->compact) return 0;
    if (append_char(scanner->out, '\n') != 0) return -1;
    for (size_t i = 0u; i < scanner->depth; ++i)
        if (append(scanner->out, "  ", 2u) != 0) return -1;
    return 0;
}

static int scan_hex4(scanner_t *scanner) {
    if (scanner->offset + 4u > scanner->length) return -1;
    for (size_t i = 0u; i < 4u; ++i) {
        char c = scanner->text[scanner->offset + i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return -1;
    }
    scanner->offset += 4u;
    return 0;
}

static int scan_string(scanner_t *scanner) {
    size_t start = scanner->offset;
    if (scanner->text[scanner->offset] != '"') return -1;
    ++scanner->offset;
    for (;;) {
        if (scanner->offset >= scanner->length) return -1;
        unsigned char c = (unsigned char)scanner->text[scanner->offset];
        if (c == '"') {
            ++scanner->offset;
            return emit(scanner, scanner->text + start, scanner->offset - start);
        }
        if (c < 0x20u) return -1;
        if (c == '\\') {
            ++scanner->offset;
            if (scanner->offset >= scanner->length) return -1;
            char escape = scanner->text[scanner->offset++];
            if (escape == 'u') {
                if (scan_hex4(scanner) != 0) return -1;
            } else if (!strchr("\"\\/bfnrt", escape)) {
                return -1;
            }
            continue;
        }
        ++scanner->offset;
    }
}

static int scan_number(scanner_t *scanner) {
    size_t start = scanner->offset;
    const char *text = scanner->text;
    size_t length = scanner->length;
    size_t i = scanner->offset;
    if (i < length && text[i] == '-') ++i;
    if (i >= length) return -1;
    if (text[i] == '0') ++i;
    else if (text[i] >= '1' && text[i] <= '9') {
        while (i < length && text[i] >= '0' && text[i] <= '9') ++i;
    } else return -1;
    if (i < length && text[i] == '.') {
        ++i;
        if (i >= length || text[i] < '0' || text[i] > '9') return -1;
        while (i < length && text[i] >= '0' && text[i] <= '9') ++i;
    }
    if (i < length && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < length && (text[i] == '+' || text[i] == '-')) ++i;
        if (i >= length || text[i] < '0' || text[i] > '9') return -1;
        while (i < length && text[i] >= '0' && text[i] <= '9') ++i;
    }
    scanner->offset = i;
    return emit(scanner, text + start, i - start);
}

static int scan_literal(scanner_t *scanner, const char *literal) {
    size_t length = strlen(literal);
    if (scanner->offset + length > scanner->length ||
        memcmp(scanner->text + scanner->offset, literal, length) != 0)
        return -1;
    scanner->offset += length;
    return emit(scanner, literal, length);
}

static int scan_value(scanner_t *scanner);

static int scan_container(scanner_t *scanner, char open, char close) {
    if (scanner->depth >= MAELYS_CLI_JSON_MAX_DEPTH) return -1;
    ++scanner->offset;
    if (emit(scanner, &open, 1u) != 0) return -1;
    ++scanner->depth;
    skip_space(scanner);
    if (scanner->offset < scanner->length &&
        scanner->text[scanner->offset] == close) {
        ++scanner->offset;
        --scanner->depth;
        return emit(scanner, &close, 1u);
    }
    for (;;) {
        if (emit_newline(scanner) != 0) return -1;
        skip_space(scanner);
        if (open == '{') {
            if (scanner->offset >= scanner->length ||
                scanner->text[scanner->offset] != '"' ||
                scan_string(scanner) != 0)
                return -1;
            skip_space(scanner);
            if (scanner->offset >= scanner->length ||
                scanner->text[scanner->offset] != ':')
                return -1;
            ++scanner->offset;
            if (emit(scanner, scanner->compact ? ":" : ": ",
                    scanner->compact ? 1u : 2u) != 0)
                return -1;
        }
        if (scan_value(scanner) != 0) return -1;
        skip_space(scanner);
        if (scanner->offset >= scanner->length) return -1;
        char next = scanner->text[scanner->offset++];
        if (next == ',') {
            if (emit(scanner, ",", 1u) != 0) return -1;
            continue;
        }
        if (next != close) return -1;
        --scanner->depth;
        if (emit_newline(scanner) != 0) return -1;
        return emit(scanner, &close, 1u);
    }
}

static int scan_value(scanner_t *scanner) {
    skip_space(scanner);
    if (scanner->offset >= scanner->length) return -1;
    char c = scanner->text[scanner->offset];
    switch (c) {
        case '{': return scan_container(scanner, '{', '}');
        case '[': return scan_container(scanner, '[', ']');
        case '"': return scan_string(scanner);
        case 't': return scan_literal(scanner, "true");
        case 'f': return scan_literal(scanner, "false");
        case 'n': return scan_literal(scanner, "null");
        default: return scan_number(scanner);
    }
}

static int scan_document(scanner_t *scanner) {
    if (scan_value(scanner) != 0) return -1;
    skip_space(scanner);
    return scanner->offset == scanner->length ? 0 : -1;
}

int maelys_cli_json_validate(
    const char *text, size_t length, size_t *out_offset) {
    if (!text) {
        if (out_offset) *out_offset = 0u;
        return -1;
    }
    scanner_t scanner = {text, length, 0u, 0u, NULL, 1};
    int result = scan_document(&scanner);
    if (out_offset) *out_offset = scanner.offset;
    return result;
}

int maelys_cli_json_format(const char *text, int compact, char **out_text) {
    if (!text || !out_text) {
        errno = EINVAL;
        return -1;
    }
    size_t length = strlen(text);
    if (maelys_cli_json_validate(text, length, NULL) != 0) {
        errno = EINVAL;
        return -1;
    }
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    scanner_t scanner = {text, length, 0u, 0u, &writer, compact};
    if (scan_document(&scanner) != 0 || writer.failed) {
        maelys_cli_json_writer_clear(&writer);
        errno = EINVAL;
        return -1;
    }
    *out_text = writer.data ? writer.data : strdup("");
    if (!*out_text) return -1;
    return 0;
}
