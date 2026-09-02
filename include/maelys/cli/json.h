#ifndef MAELYS_CLI_JSON_H
#define MAELYS_CLI_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * JSON output mechanics for CLI envelopes, catalog descriptions and
 * records: an order-preserving incremental writer, a syntax validator and
 * a formatter, with no dependency. Reading structured documents that cross
 * a trust boundary (extension manifests, configuration) is the job of
 * maelys-json; see maelys/cli/extension.h.
 */

#define MAELYS_CLI_JSON_MAX_DEPTH 64u

typedef struct maelys_cli_json_writer {
    char *data;
    size_t size;
    size_t capacity;
    int failed;
    size_t depth;
    unsigned char has_items[MAELYS_CLI_JSON_MAX_DEPTH];
    unsigned char is_object[MAELYS_CLI_JSON_MAX_DEPTH];
    int pending_key;
} maelys_cli_json_writer_t;

void maelys_cli_json_writer_init(maelys_cli_json_writer_t *writer);
void maelys_cli_json_writer_clear(maelys_cli_json_writer_t *writer);

int maelys_cli_json_begin_object(maelys_cli_json_writer_t *writer);
int maelys_cli_json_end_object(maelys_cli_json_writer_t *writer);
int maelys_cli_json_begin_array(maelys_cli_json_writer_t *writer);
int maelys_cli_json_end_array(maelys_cli_json_writer_t *writer);
int maelys_cli_json_key(maelys_cli_json_writer_t *writer, const char *key);
/* A NULL value or invalid UTF-8 is refused (the writer fails); write null
 * explicitly with maelys_cli_json_null() when the schema allows it, and
 * transcode or escape non-UTF-8 names before serializing them. */
int maelys_cli_json_string(maelys_cli_json_writer_t *writer, const char *value);
int maelys_cli_json_stringn(
    maelys_cli_json_writer_t *writer, const char *value, size_t length);
int maelys_cli_json_integer(maelys_cli_json_writer_t *writer, int64_t value);
int maelys_cli_json_unsigned(maelys_cli_json_writer_t *writer, uint64_t value);
int maelys_cli_json_boolean(maelys_cli_json_writer_t *writer, int value);
int maelys_cli_json_null(maelys_cli_json_writer_t *writer);
/* Inserts pre-serialized JSON text verbatim after validating it. */
int maelys_cli_json_raw(maelys_cli_json_writer_t *writer, const char *json);

/* Convenience: key followed by a value. */
int maelys_cli_json_key_string(
    maelys_cli_json_writer_t *writer, const char *key, const char *value);
int maelys_cli_json_key_integer(
    maelys_cli_json_writer_t *writer, const char *key, int64_t value);
int maelys_cli_json_key_unsigned(
    maelys_cli_json_writer_t *writer, const char *key, uint64_t value);
int maelys_cli_json_key_boolean(
    maelys_cli_json_writer_t *writer, const char *key, int value);
int maelys_cli_json_key_raw(
    maelys_cli_json_writer_t *writer, const char *key, const char *json);

/* Returns the finished text (ownership transferred) or NULL when any step
 * failed or a container is still open. The writer is reset. */
char *maelys_cli_json_finish(maelys_cli_json_writer_t *writer);

/* Syntax validation of one complete JSON value: RFC 8259 grammar, depth
 * at most MAELYS_CLI_JSON_MAX_DEPTH, control characters and bad escapes
 * refused. It does not check UTF-8 or duplicate keys: it guards output
 * produced by this writer or by a serializer, not untrusted input. Returns
 * 0 when valid and -1 otherwise; out_offset receives the failing byte. */
int maelys_cli_json_validate(
    const char *text, size_t length, size_t *out_offset);

/* Re-serializes valid JSON either compact or indented by two spaces. Key
 * order is preserved. The result is owned by the caller. */
int maelys_cli_json_format(const char *text, int compact, char **out_text);

/* Reading untrusted documents (manifests, configuration) is the job of
 * maelys-json (<maelys/json.h>): bounded parsing, duplicate-key and UTF-8
 * rejection, canonical bytes. libmaelys_cli_extension links it; the core
 * does not. */

#ifdef __cplusplus
}
#endif

#endif
