#include "check.h"

#include <maelys/cli/process.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *shell_path(void) {
    return access("/bin/sh", X_OK) == 0 ? "/bin/sh" : "/usr/bin/sh";
}

static int test_check_executable(void) {
    const char *explanation = NULL;
    CHECK(maelys_cli_process_check_executable(shell_path(), &explanation) == 0);
    CHECK(maelys_cli_process_check_executable("sh", &explanation) != 0);
    CHECK(explanation != NULL && errno == EINVAL);
    CHECK(maelys_cli_process_check_executable("/nonexistent/maelys-bin", &explanation) != 0);
    CHECK(maelys_cli_process_check_executable("/tmp", &explanation) != 0);
    char path[] = "/tmp/maelys-cli-exec.XXXXXX";
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "#!/bin/sh\nexit 0\n", 17u) == 17);
    CHECK(close(descriptor) == 0);
    CHECK(chmod(path, 0777) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) != 0 && errno == EPERM);
    CHECK(chmod(path, 0755) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) == 0);
    CHECK(chmod(path, 0644) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) != 0 && errno == EACCES);
    (void)unlink(path);
    return 1;
}

static int test_run(void) {
    maelys_cli_process_status_t status;
    char *exit_three[] = {"sh", "-c", "exit 3", NULL};
    CHECK(maelys_cli_process_run(shell_path(), exit_three, NULL, &status) == 0);
    CHECK(status.exited && status.exit_code == 3 && !status.signaled);
    CHECK(maelys_cli_process_exit_code(&status) == 3);
    char *killed[] = {"sh", "-c", "kill -9 $$", NULL};
    CHECK(maelys_cli_process_run(shell_path(), killed, NULL, &status) == 0);
    CHECK(status.signaled && status.term_signal == 9);
    CHECK(maelys_cli_process_exit_code(&status) == 137);
    char *environment[] = {"MAELYS_CLI_TEST_ENV=yes", NULL};
    char *check_env[] = {"sh", "-c", "test \"$MAELYS_CLI_TEST_ENV\" = yes", NULL};
    CHECK(maelys_cli_process_run(shell_path(), check_env, environment, &status) == 0);
    CHECK(status.exited && status.exit_code == 0);
    char *missing[] = {"missing", NULL};
    CHECK(maelys_cli_process_run("/nonexistent/maelys-bin", missing, NULL, &status) != 0);
    CHECK(maelys_cli_process_run("sh", missing, NULL, &status) != 0 && errno == EINVAL);
    return 1;
}

static int test_resolve_and_directory(void) {
    char resolved[1024];
    const char *directories[] = {"relative", "/nonexistent", "/bin", "/usr/bin"};
    CHECK(maelys_cli_process_resolve("sh", directories, 4u, resolved, sizeof(resolved)) == 0);
    CHECK(strcmp(resolved, "/bin/sh") == 0 || strcmp(resolved, "/usr/bin/sh") == 0);
    CHECK(maelys_cli_process_resolve("maelys-definitely-missing", directories, 4u,
        resolved, sizeof(resolved)) != 0 && errno == ENOENT);
    CHECK(maelys_cli_process_resolve("../sh", directories, 4u, resolved, sizeof(resolved)) != 0);
    CHECK(maelys_cli_process_resolve("sh", directories, 4u, resolved, 4u) != 0);
    char directory[1024];
    CHECK(maelys_cli_executable_directory("./ignored", directory, sizeof(directory)) == 0);
    CHECK(directory[0] == '/');
    struct stat status;
    CHECK(stat(directory, &status) == 0 && S_ISDIR(status.st_mode));
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_check_executable);
    RUN(test_run);
    RUN(test_resolve_and_directory);
    return failures ? 1 : 0;
}
