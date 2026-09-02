#include "check.h"

#include <maelys/cli/environment.h>
#include <maelys/cli/files.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[] = "/tmp/maelys-cli-files.XXXXXX";

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
    RUN(test_read_descriptor);
    RUN(test_environment);
    (void)rmdir(directory);
    return failures ? 1 : 0;
}
