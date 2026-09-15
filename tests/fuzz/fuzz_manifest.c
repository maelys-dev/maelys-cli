/* SPDX-License-Identifier: MPL-2.0 */
/* maelys_cli_extension_load on an arbitrary manifest: the maelys-json
 * document it parses, the RFC 6901 pointer it names on a parse failure,
 * the fields it copies into fixed buffers, the executable it resolves and
 * the digest it may verify. maelys-json fuzzes its own parser; this is what
 * the framework does with the document.
 *
 * Input: the manifest bytes, written each time to a private file the loader
 * trusts (a regular file of the current user, mode 0644). Property: an
 * accepted manifest leaves every field terminated inside its buffer and an
 * absolute executable. */
#include "maelys/cli/extension.h"
#include "replay.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int terminated(const char *field, size_t capacity) {
    return memchr(field, '\0', capacity) != NULL;
}

static char directory[PATH_MAX];
static char path[PATH_MAX];

/* The private directory goes with the process, so a replay in `make check`
 * leaves nothing in /tmp. */
static void remove_directory(void) {
    (void)unlink(path);
    (void)rmdir(directory);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!path[0]) {
        char pattern[] = "/tmp/maelys-cli-fuzz-XXXXXX";
        if (!mkdtemp(pattern) || !realpath(pattern, directory) ||
            (size_t)snprintf(path, sizeof(path), "%s/manifest.json", directory) >=
                sizeof(path))
            abort();
        (void)atexit(remove_directory);
    }
    if (size > MAELYS_CLI_EXTENSION_MAX_MANIFEST_BYTES + 1u) return 0;
    /* One file, rewritten in place: creating and unlinking a file per input
     * cost milliseconds of blocked I/O on macOS, more than the load itself.
     * The descriptor stays open for writing; the loader opens the path on
     * its own, as it would an installed manifest. */
    static int fd = -1;
    if (fd < 0) {
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd < 0) abort();
    }
    if (ftruncate(fd, 0) != 0) abort();
    for (size_t done = 0u; done < size;) {
        ssize_t written = pwrite(fd, data + done, size - done, (off_t)done);
        if (written <= 0) abort();
        done += (size_t)written;
    }

    maelys_cli_extension_t extension;
    maelys_cli_error_t error;
    if (maelys_cli_extension_load(path, &extension, &error) == 0) {
        if (!terminated(extension.command, sizeof(extension.command)) ||
            !terminated(extension.executable, sizeof(extension.executable)) ||
            !terminated(extension.manifest, sizeof(extension.manifest)) ||
            !terminated(extension.version, sizeof(extension.version)) ||
            !terminated(extension.summary, sizeof(extension.summary)) ||
            !terminated(extension.sha256, sizeof(extension.sha256)) ||
            extension.executable[0] != '/' || extension.command[0] == '\0')
            abort();
    }
    return 0;
}
