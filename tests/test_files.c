#include "check.h"

#include <maelys/cli/environment.h>
#include <maelys/cli/files.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Linux has no EFTYPE; files.c falls back to EINVAL the same way. */
#ifndef EFTYPE
#define EFTYPE EINVAL
#endif

static char directory[] = "/tmp/maelys-cli-files.XXXXXX";

/* Fault hook linked into the test copy of files.c: injects fault_errno once
 * before the named system call. */
static const char *fault_point = NULL;
static int fault_errno = 0;
int maelys_cli_test_fault(const char *point);
int maelys_cli_test_fault(const char *point) {
    if (!fault_point || strcmp(fault_point, point) != 0) return 0;
    int injected = fault_errno;
    fault_point = NULL;
    fault_errno = 0;
    return injected;
}
static void inject(const char *point, int saved_errno) {
    fault_point = point;
    fault_errno = saved_errno;
}

static int test_atomic_write_and_read(void) {
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/output", directory);
    static const unsigned char first[] = {0u, 1u, 2u, 3u};
    static const unsigned char second[] = {'o', 'k'};
    CHECK(maelys_cli_write_file_atomic(path, first, sizeof(first), 0600,
        MAELYS_CLI_WRITE_NO_REPLACE) == 0);
    errno = 0;
    CHECK(maelys_cli_write_file_atomic(path, second, sizeof(second), 0600,
        MAELYS_CLI_WRITE_NO_REPLACE) != 0 && errno == EEXIST);
    CHECK(maelys_cli_write_file_atomic(path, second, sizeof(second), 0640,
        MAELYS_CLI_WRITE_REPLACE) == 0);
    unsigned char *bytes = NULL;
    size_t size = 0u;
    CHECK(maelys_cli_read_regular_file(path, 1u, 16u, &bytes, &size) == 0);
    CHECK(size == sizeof(second) && memcmp(bytes, second, sizeof(second)) == 0);
    free(bytes);
    errno = 0;
    CHECK(maelys_cli_read_regular_file(path, 3u, 16u, &bytes, &size) != 0 && errno == EFBIG);
    CHECK(maelys_cli_read_regular_file(path, 0u, 1u, &bytes, &size) != 0 && errno == EFBIG);
    CHECK(maelys_cli_read_regular_file(directory, 0u, 16u, &bytes, &size) != 0);
    struct stat status;
    CHECK(stat(path, &status) == 0 && (status.st_mode & 0777u) == 0640u);
    CHECK(maelys_cli_write_file_atomic(path, NULL, 1u, 0600, MAELYS_CLI_WRITE_REPLACE) != 0);
    CHECK(maelys_cli_write_file_atomic(path, second, 2u, 0600, (maelys_cli_write_policy_t)7) != 0);
    /* No stray temporaries remain. */
    char temporary[512];
    (void)snprintf(temporary, sizeof(temporary), "%s/output.tmp.", directory);
    (void)unlink(path);
    return 1;
}

static int test_check_file(void) {
    char path[512];
    char link_path[512];
    const char *explanation = NULL;
    (void)snprintf(path, sizeof(path), "%s/secret", directory);
    (void)snprintf(link_path, sizeof(link_path), "%s/link", directory);
    CHECK(maelys_cli_write_file_atomic(path, "s", 1u, 0600, MAELYS_CLI_WRITE_NO_REPLACE) == 0);
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_REGULAR | MAELYS_CLI_FILE_NO_SYMLINK |
        MAELYS_CLI_FILE_OWNER_TRUSTED | MAELYS_CLI_FILE_PRIVATE, &explanation) == 0);
    CHECK(chmod(path, 0644) == 0);
    errno = 0;
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_PRIVATE, &explanation) != 0);
    CHECK(errno == EPERM && explanation != NULL);
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS, NULL) == 0);
    CHECK(chmod(path, 0666) == 0);
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS, NULL) != 0);
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_EXECUTABLE, NULL) != 0 && errno == EACCES);
    CHECK(symlink(path, link_path) == 0);
    CHECK(maelys_cli_check_file(link_path, MAELYS_CLI_FILE_REGULAR, NULL) == 0);
    CHECK(maelys_cli_check_file(link_path, MAELYS_CLI_FILE_NO_SYMLINK, NULL) != 0 && errno == ELOOP);
    CHECK(maelys_cli_check_file(directory, MAELYS_CLI_FILE_REGULAR, NULL) != 0);
    CHECK(maelys_cli_check_file("/nonexistent/maelys", MAELYS_CLI_FILE_REGULAR, NULL) != 0);
    CHECK(errno == ENOENT);
    CHECK(maelys_cli_check_file("", MAELYS_CLI_FILE_REGULAR, NULL) != 0);
    (void)unlink(link_path);
    (void)unlink(path);
    return 1;
}

static int write_plain(const char *path, const char *text, mode_t mode) {
    return maelys_cli_write_file_atomic(path, text, strlen(text), mode,
        MAELYS_CLI_WRITE_REPLACE) == 0;
}

static int test_check_file_new_requirements(void) {
    char path[512];
    char alias[512];
    const char *explanation = NULL;
    (void)snprintf(path, sizeof(path), "%s/linked", directory);
    (void)snprintf(alias, sizeof(alias), "%s/alias", directory);
    CHECK(write_plain(path, "x", 0600));
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_SINGLE_LINK |
        MAELYS_CLI_FILE_OWNER_CALLER, &explanation) == 0);
    CHECK(link(path, alias) == 0);
    errno = 0;
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_SINGLE_LINK, &explanation) != 0);
    CHECK(errno == EMLINK && strstr(explanation, "hard link") != NULL);
    CHECK(unlink(alias) == 0);
    CHECK(maelys_cli_check_file(path, MAELYS_CLI_FILE_SINGLE_LINK, NULL) == 0);
    (void)unlink(path);
    /* OWNER_CALLER refuses root-owned files unless the caller is root, where
     * OWNER_TRUSTED accepts them. */
    if (geteuid() != 0u) {
        struct stat status;
        if (stat("/", &status) == 0 && status.st_uid == 0u) {
            CHECK(maelys_cli_check_file("/", MAELYS_CLI_FILE_OWNER_TRUSTED, NULL) == 0);
            errno = 0;
            CHECK(maelys_cli_check_file("/", MAELYS_CLI_FILE_OWNER_CALLER, &explanation) != 0);
            CHECK(errno == EPERM && strstr(explanation, "caller") != NULL);
        }
    }
    return 1;
}

static int test_open_trusted(void) {
    char path[512];
    char link_path[512];
    char fifo[512];
    const char *explanation = NULL;
    int descriptor = -1;
    (void)snprintf(path, sizeof(path), "%s/config", directory);
    (void)snprintf(link_path, sizeof(link_path), "%s/config-link", directory);
    (void)snprintf(fifo, sizeof(fifo), "%s/fifo", directory);
    CHECK(write_plain(path, "key=value\n", 0600));
    CHECK(maelys_cli_open_trusted(path, MAELYS_CLI_FILE_NO_SYMLINK |
        MAELYS_CLI_FILE_OWNER_CALLER | MAELYS_CLI_FILE_PRIVATE |
        MAELYS_CLI_FILE_SINGLE_LINK, &descriptor, &explanation) == 0);
    CHECK(descriptor >= 0 && explanation == NULL);
    /* Blocking, close-on-exec, at offset zero. */
    CHECK((fcntl(descriptor, F_GETFL) & O_NONBLOCK) == 0);
    CHECK((fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK(lseek(descriptor, 0, SEEK_CUR) == 0);
    CHECK(close(descriptor) == 0);
    /* Symbolic link: refused with NO_SYMLINK, followed and judged without. */
    CHECK(symlink(path, link_path) == 0);
    errno = 0;
    CHECK(maelys_cli_open_trusted(link_path, MAELYS_CLI_FILE_NO_SYMLINK,
        &descriptor, &explanation) != 0);
    CHECK(errno == ELOOP && descriptor == -1 && strstr(explanation, "symbolic") != NULL);
    CHECK(maelys_cli_open_trusted(link_path, MAELYS_CLI_FILE_PRIVATE,
        &descriptor, &explanation) == 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(link_path) == 0);
    /* Permissions are judged on the descriptor. */
    CHECK(chmod(path, 0644) == 0);
    errno = 0;
    CHECK(maelys_cli_open_trusted(path, MAELYS_CLI_FILE_PRIVATE, &descriptor, &explanation) != 0);
    CHECK(errno == EPERM && descriptor == -1);
    CHECK(maelys_cli_open_trusted(path, MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS,
        &descriptor, NULL) == 0);
    CHECK(close(descriptor) == 0);
    /* A FIFO never blocks the open and is refused as not regular. */
    CHECK(mkfifo(fifo, 0600) == 0);
    errno = 0;
    CHECK(maelys_cli_open_trusted(fifo, 0u, &descriptor, &explanation) != 0);
    CHECK(errno == EFTYPE && strstr(explanation, "regular") != NULL);
    CHECK(unlink(fifo) == 0);
    /* Directory, missing path, empty path, missing output. */
    errno = 0;
    CHECK(maelys_cli_open_trusted(directory, 0u, &descriptor, NULL) != 0 && errno == EFTYPE);
    errno = 0;
    CHECK(maelys_cli_open_trusted("/nonexistent/maelys", 0u, &descriptor, &explanation) != 0);
    CHECK(errno == ENOENT && explanation != NULL);
    CHECK(maelys_cli_open_trusted("", 0u, &descriptor, NULL) != 0 && errno == EINVAL);
    CHECK(maelys_cli_open_trusted(path, 0u, NULL, NULL) != 0 && errno == EINVAL);
    /* Fault points: each system call of the open path. */
    inject("open", EIO);
    errno = 0;
    CHECK(maelys_cli_open_trusted(path, 0u, &descriptor, &explanation) != 0);
    CHECK(errno == EIO && descriptor == -1);
    inject("fstat", EIO);
    errno = 0;
    CHECK(maelys_cli_open_trusted(path, 0u, &descriptor, &explanation) != 0);
    CHECK(errno == EIO && strstr(explanation, "status") != NULL);
    inject("fcntl", EBADF);
    errno = 0;
    CHECK(maelys_cli_open_trusted(path, 0u, &descriptor, &explanation) != 0);
    CHECK(errno == EBADF && strstr(explanation, "flags") != NULL);
    CHECK(fault_point == NULL);
    (void)unlink(path);
    return 1;
}

static int test_read_trusted_file(void) {
    char path[512];
    const char *explanation = NULL;
    unsigned char *bytes = NULL;
    size_t size = 0u;
    (void)snprintf(path, sizeof(path), "%s/secret", directory);
    CHECK(write_plain(path, "hunter2", 0600));
    CHECK(maelys_cli_read_trusted_file(path, MAELYS_CLI_FILE_NO_SYMLINK |
        MAELYS_CLI_FILE_OWNER_CALLER | MAELYS_CLI_FILE_PRIVATE |
        MAELYS_CLI_FILE_SINGLE_LINK, 1u, 64u, &bytes, &size, &explanation) == 0);
    CHECK(size == 7u && memcmp(bytes, "hunter2", 7u) == 0 && explanation == NULL);
    maelys_cli_zero(bytes, size);
    free(bytes);
    /* Exact maximum accepted, one byte less refused before any read. */
    CHECK(maelys_cli_read_trusted_file(path, 0u, 7u, 7u, &bytes, &size, NULL) == 0);
    CHECK(size == 7u);
    free(bytes);
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 6u, &bytes, &size, &explanation) != 0);
    CHECK(errno == EFBIG && bytes == NULL && strstr(explanation, "larger") != NULL);
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 8u, 64u, &bytes, &size, &explanation) != 0);
    CHECK(errno == EFBIG && strstr(explanation, "smaller") != NULL);
    /* Invalid bounds and missing outputs. */
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 9u, 8u, &bytes, &size, &explanation) != 0);
    CHECK(errno == EINVAL && explanation != NULL);
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 8u, NULL, &size, NULL) != 0);
    /* Empty file within bounds: NULL buffer, size zero. */
    CHECK(write_plain(path, "", 0600));
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 8u, &bytes, &size, NULL) == 0);
    CHECK(bytes == NULL && size == 0u);
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 0u, &bytes, &size, NULL) == 0);
    CHECK(bytes == NULL && size == 0u);
    /* Trust failure surfaces the check, not a read error. */
    CHECK(chmod(path, 0644) == 0);
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, MAELYS_CLI_FILE_PRIVATE, 0u, 8u,
        &bytes, &size, &explanation) != 0);
    CHECK(errno == EPERM && strstr(explanation, "group") != NULL);
    /* Fault points after the open: read, realloc, close. */
    CHECK(write_plain(path, "hunter2", 0600));
    inject("read", EIO);
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 64u, &bytes, &size, &explanation) != 0);
    CHECK(errno == EIO && bytes == NULL && strstr(explanation, "readable") != NULL);
    inject("realloc", ENOMEM);
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 64u, &bytes, &size, &explanation) != 0);
    CHECK(errno == ENOMEM && strstr(explanation, "memory") != NULL);
    inject("close", EIO);
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 64u, &bytes, &size, &explanation) != 0);
    CHECK(errno == EIO && bytes == NULL && size == 0u);
    CHECK(fault_point == NULL);
    (void)unlink(path);
    return 1;
}

/* A file that grows while it is read is bounded by the bytes read, not by
 * the size observed at the open: the child appends past the maximum between
 * the parent's open and its read. */
static int test_read_growing_file(void) {
    char path[512];
    unsigned char *bytes = NULL;
    size_t size = 0u;
    (void)snprintf(path, sizeof(path), "%s/growing", directory);
    CHECK(write_plain(path, "12345678", 0600));
    int ready[2];
    int done[2];
    CHECK(pipe(ready) == 0 && pipe(done) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        char byte;
        (void)close(ready[1]);
        (void)close(done[0]);
        if (read(ready[0], &byte, 1u) == 1) {
            int fd = open(path, O_WRONLY | O_APPEND);
            if (fd >= 0) {
                (void)!write(fd, "9", 1u);
                (void)close(fd);
            }
        }
        (void)!write(done[1], "d", 1u);
        _exit(0);
    }
    (void)close(ready[0]);
    (void)close(done[1]);
    /* Open first, then let the child append, then read with the hook. */
    int descriptor = -1;
    CHECK(maelys_cli_open_trusted(path, 0u, &descriptor, NULL) == 0);
    CHECK(write(ready[1], "g", 1u) == 1);
    char byte;
    CHECK(read(done[0], &byte, 1u) == 1);
    (void)close(ready[1]);
    (void)close(done[0]);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    /* The same descriptor, read through the bounded reader. */
    errno = 0;
    CHECK(maelys_cli_read_descriptor(descriptor, 8u, &bytes, &size) != 0 && errno == EFBIG);
    CHECK(close(descriptor) == 0);
    /* And the one-call form sees nine bytes, above a maximum of eight. */
    errno = 0;
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 8u, &bytes, &size, NULL) != 0);
    CHECK(errno == EFBIG);
    CHECK(maelys_cli_read_trusted_file(path, 0u, 0u, 9u, &bytes, &size, NULL) == 0);
    CHECK(size == 9u);
    free(bytes);
    (void)unlink(path);
    return 1;
}

static int test_zero_and_codes(void) {
    unsigned char buffer[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    maelys_cli_zero(buffer, sizeof(buffer));
    for (size_t i = 0u; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0u);
    maelys_cli_zero(NULL, 4u);
    CHECK(strcmp(maelys_cli_file_error_code(ENOENT), "NOT_FOUND") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(ENOTDIR), "NOT_FOUND") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(EACCES), "ACCESS_DENIED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(EPERM), "ACCESS_DENIED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(EFBIG), "VALIDATION_FAILED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(ELOOP), "VALIDATION_FAILED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(EMLINK), "VALIDATION_FAILED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(EINVAL), "VALIDATION_FAILED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(EIO), "IO_FAILED") == 0);
    CHECK(strcmp(maelys_cli_file_error_code(0), "IO_FAILED") == 0);
    return 1;
}

static int test_read_descriptor(void) {
    int pipes[2];
    CHECK(pipe(pipes) == 0);
    CHECK(write(pipes[1], "abcdef", 6u) == 6);
    (void)close(pipes[1]);
    unsigned char *bytes = NULL;
    size_t size = 0u;
    CHECK(maelys_cli_read_descriptor(pipes[0], 16u, &bytes, &size) == 0);
    CHECK(size == 6u && memcmp(bytes, "abcdef", 6u) == 0);
    free(bytes);
    (void)close(pipes[0]);
    CHECK(pipe(pipes) == 0);
    CHECK(write(pipes[1], "abcdef", 6u) == 6);
    (void)close(pipes[1]);
    errno = 0;
    CHECK(maelys_cli_read_descriptor(pipes[0], 4u, &bytes, &size) != 0 && errno == EFBIG);
    (void)close(pipes[0]);
    return 1;
}

static int test_environment(void) {
    CHECK(setenv("MAELYS_CLI_TEST_VALUE", "imported", 1) == 0);
    (void)unsetenv("MAELYS_CLI_TEST_MISSING");
    maelys_cli_environment_t environment = {0};
    CHECK(maelys_cli_environment_append(&environment, "EXPLICIT=value=with=equals") == 0);
    CHECK(maelys_cli_environment_append(&environment, "MAELYS_CLI_TEST_VALUE") == 0);
    CHECK(maelys_cli_environment_append(&environment, "MAELYS_CLI_TEST_MISSING") != 0);
    CHECK(maelys_cli_environment_append(&environment, "9INVALID=x") != 0);
    CHECK(maelys_cli_environment_append(&environment, "=x") != 0);
    CHECK(maelys_cli_environment_append(&environment, "BAD-NAME=x") != 0);
    CHECK(environment.count == 2u);
    CHECK(strcmp(environment.entries[0].name, "EXPLICIT") == 0);
    CHECK(strcmp(environment.entries[0].value, "value=with=equals") == 0);
    CHECK(strcmp(maelys_cli_environment_get(&environment, "MAELYS_CLI_TEST_VALUE"), "imported") == 0);
    CHECK(maelys_cli_environment_get(&environment, "NOPE") == NULL);
    char **envp = NULL;
    CHECK(maelys_cli_environment_to_envp(&environment, &envp) == 0);
    CHECK(strcmp(envp[0], "EXPLICIT=value=with=equals") == 0);
    CHECK(strcmp(envp[1], "MAELYS_CLI_TEST_VALUE=imported") == 0 && envp[2] == NULL);
    maelys_cli_envp_free(envp);
    maelys_cli_environment_clear(&environment);
    CHECK(environment.entries == NULL && environment.count == 0u);
    (void)unsetenv("MAELYS_CLI_TEST_VALUE");
    return 1;
}

int main(void) {
    if (!mkdtemp(directory)) return 1;
    int failures = 0;
    RUN(test_atomic_write_and_read);
    RUN(test_check_file);
    RUN(test_check_file_new_requirements);
    RUN(test_open_trusted);
    RUN(test_read_trusted_file);
    RUN(test_read_growing_file);
    RUN(test_zero_and_codes);
    RUN(test_read_descriptor);
    RUN(test_environment);
    (void)rmdir(directory);
    return failures ? 1 : 0;
}
