/* SPDX-License-Identifier: MPL-2.0 */
/* The JSON of the core: maelys_cli_json_validate and maelys_cli_json_format
 * on arbitrary bytes, and the writer's promise that what it finishes is
 * valid JSON whatever string it was handed.
 *
 * Input: one selector byte, then the bytes. Validation guards output, not
 * untrusted input (maelys-json reads that), but it runs on every schema a
 * catalog embeds and on every raw member a handler passes, so it must not
 * read out of bounds on anything. */
#include "maelys/cli/json.h"
#include "replay.h"

#include <stdlib.h>
#include <string.h>

#define JSON_MAX_BYTES 65536u

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1u || size > JSON_MAX_BYTES) return 0;
    const char *bytes = (const char *)data + 1u;
    size_t length = size - 1u;
    if (data[0] & 1u) {
        size_t offset = 0u;
        int valid = maelys_cli_json_validate(bytes, length, &offset);
        if (valid != 0 && offset > length) abort();
        /* format reads a C string: only a NUL-free valid document is one. */
        if (valid == 0 && memchr(bytes, '\0', length) == NULL) {
            char *text = malloc(length + 1u);
            if (!text) return 0;
            memcpy(text, bytes, length);
            text[length] = '\0';
            char *formatted = NULL;
            if (maelys_cli_json_format(text, (data[0] >> 1) & 1u, &formatted) == 0) {
                if (!formatted ||
                    maelys_cli_json_validate(formatted, strlen(formatted), NULL) != 0)
                    abort();
            }
            free(formatted);
            free(text);
        }
        return 0;
    }
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    int written = maelys_cli_json_begin_object(&writer) == 0 &&
        maelys_cli_json_key(&writer, "value") == 0 &&
        maelys_cli_json_stringn(&writer, bytes, length) == 0 &&
        maelys_cli_json_end_object(&writer) == 0;
    char *text = maelys_cli_json_finish(&writer);
    if (written && text &&
        maelys_cli_json_validate(text, strlen(text), NULL) != 0) abort();
    free(text);
    maelys_cli_json_writer_clear(&writer);
    return 0;
}
