#include "check.h"

#include <maelys/cli/process.h>

#include <errno.h>
#include <fcntl.h>
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
    char directory[] = "/tmp/maelys-cli-exec.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/tool", directory) > 0);
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "#!/bin/sh\nexit 0\n", 17u) == 17);
    CHECK(close(descriptor) == 0);
    CHECK(chmod(path, 0777) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) != 0 && errno == EPERM);
    CHECK(chmod(path, 0755) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) == 0);
    descriptor = open(path, O_WRONLY | O_TRUNC);
    CHECK(descriptor >= 0);
    const char env_script[] = "#!/usr/bin/env sh\nexit 0\n";
    CHECK(write(descriptor, env_script, sizeof(env_script) - 1u) ==
        (ssize_t)(sizeof(env_script) - 1u));
    CHECK(close(descriptor) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) != 0 &&
        errno == EPERM && strstr(explanation, "PATH") != NULL);
    descriptor = open(path, O_WRONLY | O_TRUNC);
    CHECK(descriptor >= 0);
    const char relative_script[] = "#!sh\nexit 0\n";
    CHECK(write(descriptor, relative_script, sizeof(relative_script) - 1u) ==
        (ssize_t)(sizeof(relative_script) - 1u));
    CHECK(close(descriptor) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) != 0 &&
        errno == EPERM && strstr(explanation, "absolute") != NULL);
    descriptor = open(path, O_WRONLY | O_TRUNC);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "#!/bin/sh\nexit 0\n", 17u) == 17);
    CHECK(close(descriptor) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) == 0);
    CHECK(chmod(path, 0644) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) != 0 && errno == EACCES);
    CHECK(chmod(path, 0100) == 0);
    CHECK(maelys_cli_process_check_executable(path, &explanation) == 0);
    (void)unlink(path);
    (void)rmdir(directory);
    char unsafe_path[] = "/tmp/maelys-cli-unsafe-parent.XXXXXX";
    descriptor = mkstemp(unsafe_path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, "#!/bin/sh\nexit 0\n", 17u) == 17);
    CHECK(close(descriptor) == 0);
    CHECK(chmod(unsafe_path, 0700) == 0);
    CHECK(maelys_cli_process_check_executable(unsafe_path, &explanation) != 0 &&
        errno == EPERM);
    (void)unlink(unsafe_path);
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
    char directory[] = "/tmp/maelys-cli-exec-failure.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char script[1024];
    CHECK(snprintf(script, sizeof(script), "%s/tool", directory) > 0);
    int descriptor = open(script, O_WRONLY | O_CREAT | O_EXCL, 0700);
    CHECK(descriptor >= 0);
    const char body[] = "#!/definitely/missing\nexit 0\n";
    CHECK(write(descriptor, body, sizeof(body) - 1u) ==
        (ssize_t)(sizeof(body) - 1u));
    CHECK(close(descriptor) == 0);
    char *bad_interpreter[] = {"tool", NULL};
    CHECK(maelys_cli_process_run(script, bad_interpreter, NULL, &status) != 0);
    CHECK(errno == ENOENT);
    (void)unlink(script);
    (void)rmdir(directory);
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
