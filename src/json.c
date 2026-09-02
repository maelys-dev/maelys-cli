#include "maelys/cli/json.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    scanner_t scanner = {text, strlen(text), 0u, 0u, &writer, compact};
    if (scan_document(&scanner) != 0 || writer.failed) {
        maelys_cli_json_writer_clear(&writer);
        errno = EINVAL;
        return -1;
    }
    *out_text = writer.data ? writer.data : strdup("");
    if (!*out_text) return -1;
    return 0;
}

int maelys_cli_json_object_get(
    const char *text, const char *key,
    const char **out_value, size_t *out_length) {
    if (!text || !key || !out_value || !out_length) return -1;
    size_t length = strlen(text);
    if (maelys_cli_json_validate(text, length, NULL) != 0) return -1;
    scanner_t scanner = {text, length, 0u, 0u, NULL, 1};
    skip_space(&scanner);
    if (scanner.offset >= length || text[scanner.offset] != '{') return -1;
    ++scanner.offset;
    skip_space(&scanner);
    if (scanner.offset < length && text[scanner.offset] == '}') return 0;
    size_t key_length = strlen(key);
    for (;;) {
        skip_space(&scanner);
        size_t key_start = scanner.offset;
        if (scan_string(&scanner) != 0) return -1;
        size_t key_end = scanner.offset;
        /* Compare the undecoded token: catalog keys never need escapes. */
        int matches = key_end - key_start == key_length + 2u &&
            memcmp(text + key_start + 1u, key, key_length) == 0;
        skip_space(&scanner);
        ++scanner.offset; /* ':' */
        skip_space(&scanner);
        size_t value_start = scanner.offset;
        if (scan_value(&scanner) != 0) return -1;
        if (matches) {
            *out_value = text + value_start;
            *out_length = scanner.offset - value_start;
            return 1;
        }
        skip_space(&scanner);
        if (scanner.offset >= length) return -1;
        char next = text[scanner.offset++];
        if (next == '}') return 0;
        if (next != ',') return -1;
    }
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t encode_utf8(uint32_t code_point, char out[4]) {
    if (code_point < 0x80u) {
        out[0] = (char)code_point;
        return 1u;
    }
    if (code_point < 0x800u) {
        out[0] = (char)(0xC0u | (code_point >> 6));
        out[1] = (char)(0x80u | (code_point & 0x3Fu));
        return 2u;
    }
    if (code_point < 0x10000u) {
        out[0] = (char)(0xE0u | (code_point >> 12));
        out[1] = (char)(0x80u | ((code_point >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (code_point & 0x3Fu));
        return 3u;
    }
    out[0] = (char)(0xF0u | (code_point >> 18));
    out[1] = (char)(0x80u | ((code_point >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((code_point >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (code_point & 0x3Fu));
    return 4u;
}

static int read_unit(const char *token, size_t offset, uint32_t *out) {
    uint32_t value = 0u;
    for (size_t i = 0u; i < 4u; ++i) {
        int digit = hex_value(token[offset + i]);
        if (digit < 0) return -1;
        value = (value << 4) | (uint32_t)digit;
    }
    *out = value;
    return 0;
}

int maelys_cli_json_decode_string(
    const char *token, size_t length, char **out_text) {
    if (!token || !out_text || length < 2u || token[0] != '"' ||
        token[length - 1u] != '"')
        return -1;
    char *result = malloc(length); /* decoded text is never longer */
    if (!result) return -1;
    size_t written = 0u;
    size_t i = 1u;
    size_t end = length - 1u;
    while (i < end) {
        char c = token[i];
        if (c != '\\') {
            if ((unsigned char)c < 0x20u) goto failure;
            result[written++] = c;
            ++i;
            continue;
        }
        if (i + 1u >= end) goto failure;
        char escape = token[i + 1u];
        i += 2u;
        switch (escape) {
            case '"': result[written++] = '"'; break;
            case '\\': result[written++] = '\\'; break;
            case '/': result[written++] = '/'; break;
            case 'b': result[written++] = '\b'; break;
            case 'f': result[written++] = '\f'; break;
            case 'n': result[written++] = '\n'; break;
            case 'r': result[written++] = '\r'; break;
            case 't': result[written++] = '\t'; break;
            case 'u': {
                uint32_t unit = 0u;
                if (i + 4u > end || read_unit(token, i, &unit) != 0) goto failure;
                i += 4u;
                if (unit >= 0xD800u && unit <= 0xDBFFu) {
                    uint32_t low = 0u;
                    if (i + 6u > end || token[i] != '\\' || token[i + 1u] != 'u' ||
                        read_unit(token, i + 2u, &low) != 0 ||
                        low < 0xDC00u || low > 0xDFFFu)
                        goto failure;
                    i += 6u;
                    unit = 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
                    goto failure;
                }
                if (unit == 0u) goto failure;
                char encoded[4];
                size_t count = encode_utf8(unit, encoded);
                memcpy(result + written, encoded, count);
                written += count;
                break;
            }
            default: goto failure;
        }
    }
    result[written] = '\0';
    *out_text = result;
    return 0;
failure:
    free(result);
    return -1;
}

int maelys_cli_json_decode_unsigned(
    const char *token, size_t length, uint64_t *out_value) {
    if (!token || !out_value || length == 0u || length > 20u) return -1;
    uint64_t value = 0u;
    for (size_t i = 0u; i < length; ++i) {
        char c = token[i];
        if (c < '0' || c > '9') return -1;
        uint64_t digit = (uint64_t)(c - '0');
        if (value > (UINT64_MAX - digit) / 10u) return -1;
        value = value * 10u + digit;
    }
    *out_value = value;
    return 0;
}
