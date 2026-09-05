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

/* Reads one regular file whose size is within the inclusive bounds, without
 * any trust requirement (symbolic links are followed). The bound applies to
 * the bytes actually read, never to a size observed before the read. The
 * returned buffer is owned by the caller and may be NULL only for size zero.
 * Returns -1 with errno set (EFBIG for an out-of-bounds size). Prefer
 * maelys_cli_read_trusted_file() for configuration, manifests and secrets. */
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
    MAELYS_CLI_FILE_EXECUTABLE = 1u << 5,     /* owner execute bit set */
    MAELYS_CLI_FILE_SINGLE_LINK = 1u << 6,    /* exactly one hard link */
    MAELYS_CLI_FILE_OWNER_CALLER = 1u << 7    /* owned by the caller only */
};

/* Judges the path by lstat()/stat() without opening it, for files that are
 * not read here (an executable, a directory entry). Returns 0 when every
 * requested property holds. Returns -1 with errno ENOENT (missing), ELOOP
 * (symlink), EFTYPE or EINVAL (not regular), EPERM (owner or permissions),
 * EMLINK (more than one hard link) or EACCES (not executable). out_error
 * receives a short stable explanation when non-NULL. A file read after this
 * check may not be the file checked; readers use the functions below. */
int maelys_cli_check_file(
    const char *path, unsigned int requirements, const char **out_error);

/* Opens one regular file for reading and applies the requirements to the
 * descriptor actually opened (fstat), so the file judged is the file read.
 * The open never blocks: a FIFO or a device at the path is refused as not
 * regular (MAELYS_CLI_FILE_REGULAR is implied). MAELYS_CLI_FILE_NO_SYMLINK
 * opens with O_NOFOLLOW (ELOOP on a link); without it links are followed and
 * the target is judged. The descriptor is O_CLOEXEC, blocking and at offset
 * zero. Errors and out_error follow maelys_cli_check_file(). */
int maelys_cli_open_trusted(
    const char *path, unsigned int requirements, int *out_descriptor,
    const char **out_error);

/* maelys_cli_open_trusted() followed by a read of the whole file, bounded by
 * the bytes actually read: EFBIG when they exceed maximum_size or fall short
 * of minimum_size, whatever the size observed before the read. The buffer is
 * zeroed before being released on any failure after the open. */
int maelys_cli_read_trusted_file(
    const char *path, unsigned int requirements,
    size_t minimum_size, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size, const char **out_error);

/* Overwrites size bytes with zeros in a way the compiler does not elide;
 * for a secret before free(). */
void maelys_cli_zero(void *bytes, size_t size);

/* The stable agent-cli/v2 error code for an errno left by the functions of
 * this header: NOT_FOUND (ENOENT, ENOTDIR), ACCESS_DENIED (EACCES, EPERM),
 * VALIDATION_FAILED (EFBIG, ELOOP, EMLINK, EFTYPE, EINVAL, EISDIR) and
 * IO_FAILED otherwise. */
const char *maelys_cli_file_error_code(int saved_errno);

#ifdef __cplusplus
}
#endif

#endif
