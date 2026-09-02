#ifndef MAELYS_CLI_FILES_H
#define MAELYS_CLI_FILES_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum maelys_cli_write_policy {
    MAELYS_CLI_WRITE_REPLACE = 0,
    MAELYS_CLI_WRITE_NO_REPLACE = 1
} maelys_cli_write_policy_t;

/* Reads one regular file whose size is within the inclusive bounds. The
 * returned buffer is owned by the caller and may be NULL only for size zero.
 * Returns -1 with errno set (EFBIG for an out-of-bounds size). */
int maelys_cli_read_regular_file(
    const char *path, size_t minimum_size, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size);

/* Reads a descriptor (for example stdin) until EOF, refusing more than
 * maximum_size bytes. */
int maelys_cli_read_descriptor(
    int descriptor, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size);

/* Writes through a private mode-0600 temporary in the destination directory,
 * fsyncs it and publishes it atomically. The final mode and replacement policy
 * are explicit at every call site. NO_REPLACE fails with EEXIST when any
 * entry, including a dangling symbolic link, already occupies the path. */
int maelys_cli_write_file_atomic(
    const char *path, const void *bytes, size_t size, mode_t mode,
    maelys_cli_write_policy_t policy);

/* Trust checks for configuration, manifests, secrets and executables. */
enum {
    MAELYS_CLI_FILE_REGULAR = 1u << 0,        /* regular file only */
    MAELYS_CLI_FILE_NO_SYMLINK = 1u << 1,     /* final component not a link */
    MAELYS_CLI_FILE_OWNER_TRUSTED = 1u << 2,  /* owned by root or by caller */
    MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS = 1u << 3, /* no g/o write bits */
    MAELYS_CLI_FILE_PRIVATE = 1u << 4,        /* no g/o bits at all */
    MAELYS_CLI_FILE_EXECUTABLE = 1u << 5      /* owner execute bit set */
};

/* Returns 0 when every requested property holds. Returns -1 with errno
 * ENOENT (missing), ELOOP (symlink), EFTYPE or EINVAL (not regular), EPERM
 * (owner or permissions) or EACCES (not executable). out_error receives a
 * short stable explanation when non-NULL. */
int maelys_cli_check_file(
    const char *path, unsigned int requirements, const char **out_error);

#ifdef __cplusplus
}
#endif

#endif
