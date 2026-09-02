#ifndef MAELYS_CLI_DIGEST_H
#define MAELYS_CLI_DIGEST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_CLI_SHA256_SIZE 32u
#define MAELYS_CLI_SHA256_HEX_SIZE 65u

typedef struct maelys_cli_sha256 {
    uint32_t state[8];
    uint64_t length;
    unsigned char buffer[64];
    size_t buffered;
} maelys_cli_sha256_t;

void maelys_cli_sha256_init(maelys_cli_sha256_t *context);
void maelys_cli_sha256_update(
    maelys_cli_sha256_t *context, const void *bytes, size_t size);
void maelys_cli_sha256_final(
    maelys_cli_sha256_t *context, unsigned char out[MAELYS_CLI_SHA256_SIZE]);

/* Lowercase hexadecimal digest of a buffer, NUL-terminated. */
void maelys_cli_sha256_hex(
    const void *bytes, size_t size, char out[MAELYS_CLI_SHA256_HEX_SIZE]);

/* Digest of a regular file read within maximum_size. Returns -1 with errno. */
int maelys_cli_sha256_file(
    const char *path, size_t maximum_size,
    char out[MAELYS_CLI_SHA256_HEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
