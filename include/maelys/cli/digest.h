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

/* ---- fingerprint of a plan (agent-cli/v2 2.9, section 4) ------------------
 *
 * A transaction that binds its application to the reviewed plan returns
 * data.fingerprint, a `sha256:HEX` string over the action the plan describes
 * and over the state of the resources that action would touch: the same
 * writes on the same state give the same fingerprint, another write or the
 * same write on another state gives another. What goes in is the product's
 * business; this builder is how it goes in, so that two products, and the C
 * and the Python of one, cannot disagree on a framing.
 *
 * Each entry is hashed as its label, a presence byte and its value, the two
 * strings preceded by their length on eight bytes: ("ab", "c") and
 * ("a", "bc") are different entries, and an absent value is not an empty
 * one. Add the entries in a fixed order; a label says what the entry is and
 * is part of the fingerprint. */
#define MAELYS_CLI_FINGERPRINT_SIZE 72u /* "sha256:" + 64 hex digits + NUL */

typedef struct maelys_cli_fingerprint {
    maelys_cli_sha256_t hash;
} maelys_cli_fingerprint_t;

void maelys_cli_fingerprint_init(maelys_cli_fingerprint_t *fingerprint);

/* One entry. bytes NULL is an absent value, which differs from an empty
 * one; size is ignored then. */
void maelys_cli_fingerprint_add(
    maelys_cli_fingerprint_t *fingerprint, const char *label,
    const void *bytes, size_t size);

/* One entry whose value is a NUL-terminated string; NULL is absent. */
void maelys_cli_fingerprint_add_string(
    maelys_cli_fingerprint_t *fingerprint, const char *label, const char *text);

/* The state of a file the action would touch: absent when nothing is at
 * path, the sha256 of its content when it is a regular file read within
 * maximum_size. Returns -1 with errno for anything else -- a directory, a
 * file too large, a read refused -- and adds nothing: a state that cannot
 * be read is not a state to bind a plan to.
 *
 * This reads the path itself. A handler that derives what it writes from
 * what the file holds has already read it: it adds those bytes with
 * maelys_cli_fingerprint_add() instead, so that the plan is bound to the
 * state its write comes from and not to a second read of the path. */
int maelys_cli_fingerprint_add_file(
    maelys_cli_fingerprint_t *fingerprint, const char *label,
    const char *path, size_t maximum_size);

/* Writes `sha256:HEX`, NUL-terminated. The builder is spent. */
void maelys_cli_fingerprint_finish(
    maelys_cli_fingerprint_t *fingerprint,
    char out[MAELYS_CLI_FINGERPRINT_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
