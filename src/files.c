#include "maelys/cli/files.h"
#include "maelys/cli/invocation.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef EFTYPE
#define EFTYPE EINVAL
#endif

/* Fault points: a test build defines MAELYS_CLI_FAULT_HOOK to a function
 * int hook(const char *point) returning the errno to inject before the named
 * system call, 0 for none. A release build compiles every point away. */
#ifdef MAELYS_CLI_FAULT_HOOK
int MAELYS_CLI_FAULT_HOOK(const char *point);
static int faulted(const char *point) {
    int injected = MAELYS_CLI_FAULT_HOOK(point);
    if (injected == 0) return 0;
    errno = injected;
    return 1;
}
#else
#define faulted(point) 0
#endif

static int close_preserving_error(int descriptor, int result) {
    int saved = errno;
    if ((faulted("close") || close(descriptor) != 0) && result == 0) {
        saved = errno;
        result = -1;
    }
    errno = saved;
    return result;
}

void maelys_cli_zero(void *bytes, size_t size) {
    volatile unsigned char *cursor = (volatile unsigned char *)bytes;
    if (!bytes) return;
    while (size-- > 0u) *cursor++ = 0u;
}

const char *maelys_cli_file_error_code(int saved_errno) {
    switch (saved_errno) {
    case ENOENT:
    case ENOTDIR:
        return MAELYS_CLI_CODE_NOT_FOUND;
    case EACCES:
    case EPERM:
        return MAELYS_CLI_CODE_ACCESS_DENIED;
    case EFBIG:
    case ELOOP:
    case EMLINK:
    case EINVAL:
    case EISDIR:
#if EFTYPE != EINVAL
    case EFTYPE:
#endif
        return MAELYS_CLI_CODE_VALIDATION_FAILED;
    default:
        return MAELYS_CLI_CODE_IO_FAILED;
    }
}

/* Reads a descriptor until EOF into a buffer that grows from initial_capacity
 * (0 for the default) and never holds more than maximum_size bytes: one
 * more byte than the maximum is EFBIG. The buffer is zeroed before release
 * on any failure. */
static int read_bounded(
    int descriptor, size_t maximum_size, size_t initial_capacity,
    unsigned char **out_bytes, size_t *out_size) {
    size_t capacity = 0u;
    size_t size = 0u;
    unsigned char *bytes = NULL;
    int saved = 0;
    for (;;) {
        if (size == capacity) {
            if (capacity >= maximum_size) {
                unsigned char probe;
                ssize_t extra = faulted("read") ? -1 : read(descriptor, &probe, 1u);
                if (extra < 0 && errno == EINTR) continue;
                if (extra == 0) break;
                saved = extra > 0 ? EFBIG : errno;
                goto fail;
            }
            size_t grown = capacity ? capacity * 2u :
                initial_capacity ? initial_capacity : 4096u;
            if (grown > maximum_size) grown = maximum_size;
            unsigned char *reallocated =
                faulted("realloc") ? NULL : realloc(bytes, grown);
            if (!reallocated) {
                saved = ENOMEM;
                goto fail;
            }
            bytes = reallocated;
            capacity = grown;
        }
        ssize_t amount = faulted("read") ? -1 :
            read(descriptor, bytes + size, capacity - size);
        if (amount > 0) size += (size_t)amount;
        else if (amount < 0 && errno == EINTR) continue;
        else if (amount == 0) break;
        else {
            saved = errno;
            goto fail;
        }
    }
    if (size == 0u) {
        free(bytes);
        bytes = NULL;
    }
    *out_bytes = bytes;
    *out_size = size;
    return 0;
fail:
    maelys_cli_zero(bytes, capacity);
    free(bytes);
    errno = saved;
    return -1;
}

/* Applies the requirements that a status can answer. NO_SYMLINK is judged by
 * the caller, on lstat() or on O_NOFOLLOW. */
static const char *judge_status(
    const struct stat *status, unsigned int requirements, int *out_errno) {
    if ((requirements & MAELYS_CLI_FILE_REGULAR) && !S_ISREG(status->st_mode)) {
        *out_errno = EFTYPE;
        return "path is not a regular file";
    }
    if ((requirements & MAELYS_CLI_FILE_OWNER_CALLER) &&
        status->st_uid != geteuid()) {
        *out_errno = EPERM;
        return "file is not owned by the caller";
    }
    if ((requirements & MAELYS_CLI_FILE_OWNER_TRUSTED) &&
        status->st_uid != 0u && status->st_uid != geteuid()) {
        *out_errno = EPERM;
        return "file is owned by an untrusted user";
    }
    if ((requirements & MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS) &&
        (status->st_mode & (S_IWGRP | S_IWOTH))) {
        *out_errno = EPERM;
        return "file is writable by group or world";
    }
    if ((requirements & MAELYS_CLI_FILE_PRIVATE) &&
        (status->st_mode & (S_IRWXG | S_IRWXO))) {
        *out_errno = EPERM;
        return "file grants permissions to group or world";
    }
    if ((requirements & MAELYS_CLI_FILE_SINGLE_LINK) && status->st_nlink != 1u) {
        *out_errno = EMLINK;
        return "file has more than one hard link";
    }
    if ((requirements & MAELYS_CLI_FILE_EXECUTABLE) &&
        !(status->st_mode & S_IXUSR)) {
        *out_errno = EACCES;
        return "file is not executable";
    }
    return NULL;
}

static int open_trusted_status(
    const char *path, unsigned int requirements, int *out_descriptor,
    struct stat *out_status, const char **out_error) {
    const char *explanation = NULL;
    int saved = 0;
    int descriptor = -1;
    if (out_error) *out_error = NULL;
    if (out_descriptor) *out_descriptor = -1;
    if (!path || !*path || !out_descriptor) {
        explanation = "path is empty";
        saved = EINVAL;
    } else {
        int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
        if (requirements & MAELYS_CLI_FILE_NO_SYMLINK) flags |= O_NOFOLLOW;
        descriptor = faulted("open") ? -1 : open(path, flags);
        if (descriptor < 0) {
            saved = errno == EMLINK ? ELOOP : errno;
            explanation = saved == ELOOP ? "path is a symbolic link" :
                "path does not exist or is not accessible";
        } else if (faulted("fstat") || fstat(descriptor, out_status) != 0) {
            saved = errno;
            explanation = "file status is not accessible";
        } else {
            explanation = judge_status(out_status,
                requirements | MAELYS_CLI_FILE_REGULAR, &saved);
            if (!explanation) {
                int current = faulted("fcntl") ? -1 : fcntl(descriptor, F_GETFL);
                if (current < 0 ||
                    fcntl(descriptor, F_SETFL, current & ~O_NONBLOCK) != 0) {
                    saved = errno;
                    explanation = "descriptor flags are not adjustable";
                }
            }
        }
    }
    if (out_error) *out_error = explanation;
    if (explanation) {
        if (descriptor >= 0) (void)close(descriptor);
        errno = saved;
        return -1;
    }
    *out_descriptor = descriptor;
    return 0;
}

int maelys_cli_open_trusted(
    const char *path, unsigned int requirements, int *out_descriptor,
    const char **out_error) {
    struct stat status;
    return open_trusted_status(path, requirements, out_descriptor, &status,
        out_error);
}

int maelys_cli_read_trusted_file(
    const char *path, unsigned int requirements,
    size_t minimum_size, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size, const char **out_error) {
    if (out_error) *out_error = NULL;
    if (!out_bytes || !out_size || minimum_size > maximum_size) {
        if (out_error) *out_error = "size bounds are invalid";
        errno = EINVAL;
        return -1;
    }
    *out_bytes = NULL;
    *out_size = 0u;
    int descriptor = -1;
    struct stat status;
    if (open_trusted_status(path, requirements, &descriptor, &status,
            out_error) != 0)
        return -1;
    const char *explanation = NULL;
    int saved = 0;
    unsigned char *bytes = NULL;
    size_t size = 0u;
    if (status.st_size < 0 || (uintmax_t)status.st_size > maximum_size) {
        saved = EFBIG;
        explanation = "file is larger than the maximum size";
    } else {
        /* The size observed is a capacity hint only: one byte more than the
         * file, so a file that grows meanwhile is seen and refused. */
        size_t hint = (size_t)status.st_size < maximum_size ?
            (size_t)status.st_size + 1u : maximum_size;
        if (read_bounded(descriptor, maximum_size, hint, &bytes, &size) != 0) {
            saved = errno;
            explanation = saved == EFBIG ? "file is larger than the maximum size" :
                saved == ENOMEM ? "memory is exhausted" : "file is not readable";
        } else if (size < minimum_size) {
            saved = EFBIG;
            explanation = "file is smaller than the minimum size";
        }
    }
    int result = explanation ? -1 : 0;
    errno = saved;
    result = close_preserving_error(descriptor, result);
    if (result != 0) {
        if (!explanation) explanation = "file is not readable";
        maelys_cli_zero(bytes, size);
        free(bytes);
        if (out_error) *out_error = explanation;
        return -1;
    }
    *out_bytes = bytes;
    *out_size = size;
    return 0;
}

int maelys_cli_read_regular_file(
    const char *path, size_t minimum_size, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    return maelys_cli_read_trusted_file(path, 0u, minimum_size, maximum_size,
        out_bytes, out_size, NULL);
}

int maelys_cli_read_descriptor(
    int descriptor, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size) {
    if (descriptor < 0 || !out_bytes || !out_size) {
        errno = EINVAL;
        return -1;
    }
    *out_bytes = NULL;
    *out_size = 0u;
    return read_bounded(descriptor, maximum_size, 0u, out_bytes, out_size);
}

int maelys_cli_write_file_atomic(
    const char *path, const void *bytes, size_t size, mode_t mode,
    maelys_cli_write_policy_t policy) {
    if (!path || (!bytes && size != 0u) || mode == 0u ||
        (policy != MAELYS_CLI_WRITE_REPLACE &&
         policy != MAELYS_CLI_WRITE_NO_REPLACE)) {
        errno = EINVAL;
        return -1;
    }
    if (policy == MAELYS_CLI_WRITE_NO_REPLACE) {
        struct stat existing;
        if (lstat(path, &existing) == 0) {
            errno = EEXIST;
            return -1;
        }
        if (errno != ENOENT) return -1;
    }
    size_t path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(".tmp.XXXXXX")) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char *temporary = malloc(path_length + sizeof(".tmp.XXXXXX"));
    if (!temporary) return -1;
    (void)snprintf(temporary, path_length + sizeof(".tmp.XXXXXX"),
        "%s.tmp.XXXXXX", path);
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        free(temporary);
        return -1;
    }
    int result = 0;
    if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0 ||
        fchmod(descriptor, mode) != 0)
        result = -1;
    size_t offset = 0u;
    while (result == 0 && offset < size) {
        ssize_t amount = write(descriptor,
            (const unsigned char *)bytes + offset, size - offset);
        if (amount > 0) offset += (size_t)amount;
        else if (amount < 0 && errno == EINTR) continue;
        else result = -1;
    }
    if (result == 0 && fsync(descriptor) != 0) result = -1;
    result = close_preserving_error(descriptor, result);
    if (result == 0) {
        if (policy == MAELYS_CLI_WRITE_REPLACE)
            result = rename(temporary, path);
        else if (link(temporary, path) != 0)
            result = -1;
        else
            /* Publication already succeeded. A failed best-effort unlink may
             * leave a private temporary hardlink, but must not report an
             * ambiguous failure after the destination became visible. */
            (void)unlink(temporary);
    }
    int saved = errno;
    if (result != 0) (void)unlink(temporary);
    free(temporary);
    errno = result == 0 ? 0 : (saved ? saved : EIO);
    return result;
}

int maelys_cli_check_file(
    const char *path, unsigned int requirements, const char **out_error) {
    const char *explanation = NULL;
    int saved = 0;
    if (!path || !*path) {
        explanation = "path is empty";
        saved = EINVAL;
    } else {
        struct stat status;
        if (lstat(path, &status) != 0) {
            saved = errno;
            explanation = "path does not exist or is not accessible";
        } else if ((requirements & MAELYS_CLI_FILE_NO_SYMLINK) &&
                   S_ISLNK(status.st_mode)) {
            saved = ELOOP;
            explanation = "path is a symbolic link";
        } else if (S_ISLNK(status.st_mode) && stat(path, &status) != 0) {
            saved = errno;
            explanation = "symbolic link target is not accessible";
        } else {
            explanation = judge_status(&status, requirements, &saved);
        }
    }
    if (out_error) *out_error = explanation;
    if (explanation) {
        errno = saved;
        return -1;
    }
    return 0;
}
