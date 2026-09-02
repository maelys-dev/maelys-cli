#include "maelys/cli/files.h"

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

static int close_preserving_error(int descriptor, int result) {
    int saved = errno;
    if (close(descriptor) != 0 && result == 0) {
        saved = errno;
        result = -1;
    }
    errno = saved;
    return result;
}

static int read_fully(
    int descriptor, unsigned char *bytes, size_t size) {
    size_t offset = 0u;
    while (offset < size) {
        ssize_t amount = read(descriptor, bytes + offset, size - offset);
        if (amount > 0) offset += (size_t)amount;
        else if (amount < 0 && errno == EINTR) continue;
        else {
            errno = amount == 0 ? EIO : errno;
            return -1;
        }
    }
    return 0;
}

int maelys_cli_read_regular_file(
    const char *path, size_t minimum_size, size_t maximum_size,
    unsigned char **out_bytes, size_t *out_size) {
    if (!path || !out_bytes || !out_size || minimum_size > maximum_size) {
        errno = EINVAL;
        return -1;
    }
    *out_bytes = NULL;
    *out_size = 0u;
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    struct stat status;
    if (fstat(descriptor, &status) != 0) {
        int saved = errno;
        (void)close(descriptor);
        errno = saved;
        return -1;
    }
    if (!S_ISREG(status.st_mode) || status.st_size < 0 ||
        (uintmax_t)status.st_size > SIZE_MAX) {
        (void)close(descriptor);
        errno = S_ISREG(status.st_mode) ? EFBIG : EFTYPE;
        return -1;
    }
    size_t size = (size_t)status.st_size;
    if (size < minimum_size || size > maximum_size) {
        (void)close(descriptor);
        errno = EFBIG;
        return -1;
    }
    unsigned char *bytes = size ? malloc(size) : NULL;
    if (size && !bytes) {
        (void)close(descriptor);
        return -1;
    }
    if (read_fully(descriptor, bytes, size) != 0) {
        int saved = errno;
        free(bytes);
        (void)close(descriptor);
        errno = saved;
        return -1;
    }
    if (close(descriptor) != 0) {
        int saved = errno;
        free(bytes);
        errno = saved;
        return -1;
    }
    *out_bytes = bytes;
    *out_size = size;
    return 0;
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
    size_t capacity = 0u;
    size_t size = 0u;
    unsigned char *bytes = NULL;
    for (;;) {
        if (size == capacity) {
            if (capacity >= maximum_size) {
                unsigned char probe;
                ssize_t extra = read(descriptor, &probe, 1u);
                if (extra < 0 && errno == EINTR) continue;
                if (extra == 0) break;
                free(bytes);
                errno = extra > 0 ? EFBIG : errno;
                return -1;
            }
            size_t grown = capacity ? capacity * 2u : 4096u;
            if (grown > maximum_size) grown = maximum_size;
            unsigned char *reallocated = realloc(bytes, grown);
            if (!reallocated) {
                free(bytes);
                errno = ENOMEM;
                return -1;
            }
            bytes = reallocated;
            capacity = grown;
        }
        ssize_t amount = read(descriptor, bytes + size, capacity - size);
        if (amount > 0) size += (size_t)amount;
        else if (amount < 0 && errno == EINTR) continue;
        else if (amount == 0) break;
        else {
            int saved = errno;
            free(bytes);
            errno = saved;
            return -1;
        }
    }
    *out_bytes = bytes;
    *out_size = size;
    return 0;
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
        } else {
            if (S_ISLNK(status.st_mode) && stat(path, &status) != 0) {
                saved = errno;
                explanation = "symbolic link target is not accessible";
            } else if ((requirements & MAELYS_CLI_FILE_REGULAR) &&
                       !S_ISREG(status.st_mode)) {
                saved = EFTYPE;
                explanation = "path is not a regular file";
            } else if ((requirements & MAELYS_CLI_FILE_OWNER_TRUSTED) &&
                       status.st_uid != 0u && status.st_uid != geteuid()) {
                saved = EPERM;
                explanation = "file is owned by an untrusted user";
            } else if ((requirements & MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS) &&
                       (status.st_mode & (S_IWGRP | S_IWOTH))) {
                saved = EPERM;
                explanation = "file is writable by group or world";
            } else if ((requirements & MAELYS_CLI_FILE_PRIVATE) &&
                       (status.st_mode & (S_IRWXG | S_IRWXO))) {
                saved = EPERM;
                explanation = "file grants permissions to group or world";
            } else if ((requirements & MAELYS_CLI_FILE_EXECUTABLE) &&
                       !(status.st_mode & S_IXUSR)) {
                saved = EACCES;
                explanation = "file is not executable";
            }
        }
    }
    if (out_error) *out_error = explanation;
    if (explanation) {
        errno = saved;
        return -1;
    }
    return 0;
}
