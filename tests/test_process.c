#include "check.h"

#include <maelys/cli/process.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>
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

/* A started program keeps the caller's working directory (0.5.16 regression:
 * the pathname fallback changed to the executable's directory). */
static int test_run_keeps_working_directory(void) {
    char scripts[] = "/tmp/maelys-cli-exec.XXXXXX";
    char work[] = "/tmp/maelys-cli-cwd.XXXXXX";
    CHECK(mkdtemp(scripts) && mkdtemp(work));
    char script[512];
    char report[512];
    (void)snprintf(script, sizeof(script), "%s/where", scripts);
    (void)snprintf(report, sizeof(report), "%s/report", scripts);
    int descriptor = open(script, O_WRONLY | O_CREAT | O_EXCL, 0755);
    CHECK(descriptor >= 0);
    static const char body[] = "#!/bin/sh\npwd > \"$1\"\n";
    CHECK(write(descriptor, body, sizeof(body) - 1u) == (ssize_t)(sizeof(body) - 1u));
    CHECK(close(descriptor) == 0);
    char previous[PATH_MAX];
    CHECK(getcwd(previous, sizeof(previous)));
    CHECK(chdir(work) == 0);
    char expected[PATH_MAX];
    CHECK(getcwd(expected, sizeof(expected)));
    char *argv[] = {script, report, NULL};
    maelys_cli_process_status_t status;
    int run = maelys_cli_process_run(script, argv, NULL, &status);
    CHECK(chdir(previous) == 0);
    CHECK(run == 0 && maelys_cli_process_exit_code(&status) == 0);
    FILE *stream = fopen(report, "r");
    CHECK(stream);
    char seen[PATH_MAX] = "";
    CHECK(fgets(seen, sizeof(seen), stream));
    (void)fclose(stream);
    seen[strcspn(seen, "\n")] = '\0';
    CHECK(strcmp(seen, expected) == 0);
    (void)unlink(report);
    (void)unlink(script);
    (void)rmdir(scripts);
    (void)rmdir(work);
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

/* The mapping is applied as a whole: a program started with descriptors on
 * 4 and 5 reads what the caller put in them, including when the mapping
 * exchanges two numbers the caller already holds, and when one source reaches
 * two targets. The shell reads each target and prints what it found. */
static int write_temporary(const char *text) {
    char path[] = "/tmp/maelys-cli-inherit.XXXXXX";
    int descriptor = mkstemp(path);
    if (descriptor < 0) return -1;
    (void)unlink(path);
    size_t length = strlen(text);
    if (write(descriptor, text, length) != (ssize_t)length ||
        lseek(descriptor, 0, SEEK_SET) != 0) {
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static int read_through_shell(
    const maelys_cli_process_inherit_t *inherit, size_t count,
    const char *script, char *out, size_t out_size) {
    int output[2];
    if (pipe(output) != 0) return -1;
    maelys_cli_process_inherit_t mapping[MAELYS_CLI_PROCESS_MAX_INHERIT + 1];
    for (size_t i = 0u; i < count; ++i) mapping[i] = inherit[i];
    /* The program writes on 3, which this mapping installs like any other. */
    mapping[count].source = output[1];
    mapping[count].target = 3;
    maelys_cli_process_options_t options;
    memset(&options, 0, sizeof(options));
    options.inherit = mapping;
    options.inherit_count = count + 1u;
    char *argv[] = {(char *)"sh", (char *)"-c", (char *)script, NULL};
    maelys_cli_process_t *process = NULL;
    if (maelys_cli_process_start(shell_path(), argv, NULL, &options,
            &process) != 0) {
        (void)close(output[0]);
        (void)close(output[1]);
        return -1;
    }
    (void)close(output[1]);
    maelys_cli_process_status_t status;
    int waited = maelys_cli_process_wait(process, &status);
    maelys_cli_process_release(process);
    ssize_t amount = read(output[0], out, out_size - 1u);
    (void)close(output[0]);
    if (waited != 0 || amount < 0) return -1;
    out[amount] = '\0';
    return status.exited ? status.exit_code : -1;
}

static int test_inherited_descriptors(void) {
    int first = write_temporary("alpha");
    int second = write_temporary("beta");
    CHECK(first >= 0 && second >= 0);
    char out[128];

    /* One source, one target above 2. */
    maelys_cli_process_inherit_t one[] = {{first, 4}};
    CHECK(read_through_shell(one, 1u,
        "head -c 5 <&4 >&3", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "alpha") == 0);

    /* Two sources exchanged: 4 -> 5 and 5 -> 4, which installing one target
     * after the other would have collapsed. */
    CHECK(lseek(first, 0, SEEK_SET) == 0 && lseek(second, 0, SEEK_SET) == 0);
    /* Forced onto 4 and 5, so the mapping below really exchanges two numbers
     * the caller holds; 3 is left to the output pipe of the helper above.
     * `second` is duplicated first: it may itself sit on 4, which the other
     * dup2 would then close under it. */
    int high = dup2(second, 5);
    int low = dup2(first, 4);
    CHECK(low == 4 && high == 5);
    maelys_cli_process_inherit_t swap[] = {{low, 5}, {high, 4}};
    CHECK(read_through_shell(swap, 2u,
        "{ head -c 4 <&4; head -c 5 <&5; } >&3", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "betaalpha") == 0);

    /* One source, two targets. */
    CHECK(lseek(low, 0, SEEK_SET) == 0);
    maelys_cli_process_inherit_t twice[] = {{low, 4}, {low, 5}};
    CHECK(read_through_shell(twice, 2u,
        "head -c 2 <&4 >&3; head -c 3 <&5 >&3", out, sizeof(out)) == 0);
    /* Two targets of one source share its offset, as two dups of a
     * descriptor always do: the second read continues the first. */
    CHECK(strcmp(out, "alpha") == 0);

    /* A target equal to its own source keeps nothing of close-on-exec. */
    CHECK(lseek(low, 0, SEEK_SET) == 0);
    maelys_cli_process_inherit_t same[] = {{low, low}};
    CHECK(fcntl(low, F_SETFD, FD_CLOEXEC) == 0);
    CHECK(read_through_shell(same, 1u, "head -c 5 <&4 >&3", out,
        sizeof(out)) == 0);
    CHECK(strcmp(out, "alpha") == 0);

    (void)close(low);
    (void)close(high);
    (void)close(first);
    (void)close(second);
    return 1;
}

static int test_inherited_refusals(void) {
    maelys_cli_process_t *process = NULL;
    char *argv[] = {(char *)"sh", (char *)"-c", (char *)"exit 0", NULL};
    maelys_cli_process_options_t options;
    memset(&options, 0, sizeof(options));

    /* A target below 3 is refused: 0, 1 and 2 are the program's own. */
    maelys_cli_process_inherit_t low_target[] = {{0, 2}};
    options.inherit = low_target;
    options.inherit_count = 1u;
    CHECK(maelys_cli_process_start(shell_path(), argv, NULL, &options,
        &process) != 0 && errno == EINVAL && process == NULL);

    /* Two entries naming one target. */
    maelys_cli_process_inherit_t duplicate[] = {{0, 4}, {1, 4}};
    options.inherit = duplicate;
    options.inherit_count = 2u;
    CHECK(maelys_cli_process_start(shell_path(), argv, NULL, &options,
        &process) != 0 && errno == EINVAL);

    /* A source nothing holds open. */
    maelys_cli_process_inherit_t closed[] = {{4096, 4}};
    options.inherit = closed;
    options.inherit_count = 1u;
    CHECK(maelys_cli_process_start(shell_path(), argv, NULL, &options,
        &process) != 0 && errno == EBADF);

    /* More entries than the contract accepts. */
    maelys_cli_process_inherit_t many[MAELYS_CLI_PROCESS_MAX_INHERIT + 1];
    for (size_t i = 0u; i < sizeof(many) / sizeof(many[0]); ++i) {
        many[i].source = 0;
        many[i].target = (int)(3u + i);
    }
    options.inherit = many;
    options.inherit_count = sizeof(many) / sizeof(many[0]);
    CHECK(maelys_cli_process_start(shell_path(), argv, NULL, &options,
        &process) != 0 && errno == EINVAL);
    return 1;
}

static int test_signal_and_wait(void) {
    char *argv[] = {(char *)"sh", (char *)"-c", (char *)"sleep 30", NULL};
    maelys_cli_process_t *process = NULL;
    CHECK(maelys_cli_process_start(shell_path(), argv, NULL, NULL,
        &process) == 0 && process != NULL);
    CHECK(maelys_cli_process_signal(process, SIGTERM) == 0);
    maelys_cli_process_status_t status;
    CHECK(maelys_cli_process_wait(process, &status) == 0);
    CHECK(status.signaled && status.term_signal == SIGTERM);
    /* Waited again, the handle reports what it noted and waits for nothing. */
    maelys_cli_process_status_t again;
    CHECK(maelys_cli_process_wait(process, &again) == 0);
    CHECK(again.signaled && again.term_signal == SIGTERM);
    /* The program is gone and its id is still held: nothing is sent. */
    CHECK(maelys_cli_process_signal(process, SIGTERM) != 0 && errno == ESRCH);
    maelys_cli_process_release(process);

    /* An exec that never happened owns nothing: no handle, and no child left
     * for the caller to reap. */
    char *missing[] = {(char *)"maelys-bin", NULL};
    maelys_cli_process_t *refused = (maelys_cli_process_t *)(void *)&status;
    CHECK(maelys_cli_process_start("/nonexistent/maelys-bin", missing, NULL,
        NULL, &refused) != 0);
    CHECK(refused == NULL);
    CHECK(maelys_cli_process_signal(NULL, SIGTERM) != 0 && errno == EINVAL);
    maelys_cli_process_release(NULL);
    return 1;
}

/* signal in one thread while wait blocks in another, which is what the
 * contract promises and what maelys-egress does from its sigwait thread. Run
 * under ThreadSanitizer by `make tsan-check`, where a plain int flag was a
 * data race; run here for the behaviour: the program dies of the signal, the
 * waiter reports it, and every signal after the exit is refused. */
typedef struct signaller {
    maelys_cli_process_t *process;
    int refused_after_exit;
    int sent;
} signaller_t;

static void *signal_until_refused(void *argument) {
    signaller_t *state = argument;
    /* The first one ends the program; the rest race the waiter's store. */
    if (maelys_cli_process_signal(state->process, SIGTERM) == 0) state->sent = 1;
    for (int i = 0; i < 2000; ++i) {
        if (maelys_cli_process_signal(state->process, 0) != 0) {
            state->refused_after_exit = errno == ESRCH;
            break;
        }
    }
    return NULL;
}

static int test_signal_while_waiting(void) {
    char *argv[] = {(char *)"sh", (char *)"-c", (char *)"sleep 30", NULL};
    maelys_cli_process_t *process = NULL;
    CHECK(maelys_cli_process_start(shell_path(), argv, NULL, NULL,
        &process) == 0);
    signaller_t state;
    memset(&state, 0, sizeof(state));
    state.process = process;
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, signal_until_refused, &state) == 0);
    maelys_cli_process_status_t status;
    CHECK(maelys_cli_process_wait(process, &status) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(state.sent && status.signaled && status.term_signal == SIGTERM);
    /* The signaller either stopped on ESRCH or exhausted its loop while the
     * program was still dying; both are the contract, neither is a race. */
    CHECK(maelys_cli_process_signal(process, 0) != 0 && errno == ESRCH);
    maelys_cli_process_release(process);
    return 1;
}

int main(void) {
    int failures = 0;
    RUN(test_check_executable);
    RUN(test_run_keeps_working_directory);
    RUN(test_run);
    RUN(test_inherited_descriptors);
    RUN(test_inherited_refusals);
    RUN(test_signal_and_wait);
    RUN(test_signal_while_waiting);
    RUN(test_resolve_and_directory);
    return failures ? 1 : 0;
}
