#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "maelys/cli/process.h"
#include "maelys/cli/files.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

extern char **environ;

int maelys_cli_process_check_executable(
    const char *path, const char **out_error) {
    if (!path || path[0] != '/') {
        if (out_error) *out_error = "executable path must be absolute";
        errno = EINVAL;
        return -1;
    }
    return maelys_cli_check_file(path,
        MAELYS_CLI_FILE_REGULAR | MAELYS_CLI_FILE_OWNER_TRUSTED |
        MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS | MAELYS_CLI_FILE_EXECUTABLE,
        out_error);
}

static void close_inherited_descriptors(void) {
    long maximum = sysconf(_SC_OPEN_MAX);
    if (maximum < 0 || maximum > 65536l) maximum = 65536l;
    for (int descriptor = 3; descriptor < (int)maximum; ++descriptor) {
        int flags = fcntl(descriptor, F_GETFD);
        if (flags >= 0 && !(flags & FD_CLOEXEC)) (void)close(descriptor);
    }
}

static void reset_signals(void) {
    sigset_t mask;
    (void)sigemptyset(&mask);
    (void)sigprocmask(SIG_SETMASK, &mask, NULL);
    (void)signal(SIGPIPE, SIG_DFL);
    (void)signal(SIGINT, SIG_DFL);
    (void)signal(SIGTERM, SIG_DFL);
    (void)signal(SIGHUP, SIG_DFL);
}

int maelys_cli_process_run(
    const char *path, char *const argv[], char *const envp[],
    maelys_cli_process_status_t *out_status) {
    if (!path || !argv || !out_status) {
        errno = EINVAL;
        return -1;
    }
    memset(out_status, 0, sizeof(*out_status));
    if (maelys_cli_process_check_executable(path, NULL) != 0) return -1;
    int error_pipe[2];
    if (pipe(error_pipe) != 0) return -1;
    (void)fcntl(error_pipe[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC);
    (void)fflush(stdout);
    (void)fflush(stderr);
    pid_t child = fork();
    if (child < 0) {
        int saved = errno;
        (void)close(error_pipe[0]);
        (void)close(error_pipe[1]);
        errno = saved;
        return -1;
    }
    if (child == 0) {
        (void)close(error_pipe[0]);
        reset_signals();
        close_inherited_descriptors();
        execve(path, argv, envp ? envp : environ);
        int saved = errno;
        unsigned char code = (unsigned char)(saved > 255 ? 255 : saved);
        /* A failed report cannot be acted upon here; the parent then sees
         * exit status 127 without a captured errno. */
        ssize_t reported = write(error_pipe[1], &code, 1u);
        _exit(reported == 1 ? 127 : 126);
    }
    (void)close(error_pipe[1]);
    unsigned char failure = 0u;
    ssize_t amount;
    do {
        amount = read(error_pipe[0], &failure, 1u);
    } while (amount < 0 && errno == EINTR);
    (void)close(error_pipe[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) return -1;
    if (amount == 1) {
        errno = failure;
        return -1;
    }
    if (WIFEXITED(status)) {
        out_status->exited = 1;
        out_status->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        out_status->signaled = 1;
        out_status->term_signal = WTERMSIG(status);
    }
    return 0;
}

int maelys_cli_process_replace(
    const char *path, char *const argv[], char *const envp[]) {
    if (!path || !argv) {
        errno = EINVAL;
        return -1;
    }
    if (maelys_cli_process_check_executable(path, NULL) != 0) return -1;
    (void)fflush(stdout);
    (void)fflush(stderr);
    close_inherited_descriptors();
    execve(path, argv, envp ? envp : environ);
    return -1;
}

int maelys_cli_process_exit_code(const maelys_cli_process_status_t *status) {
    if (!status) return 1;
    if (status->signaled) return 128 + status->term_signal;
    return status->exit_code;
}

int maelys_cli_process_resolve(
    const char *name, const char *const *directories, size_t directory_count,
    char *out_path, size_t out_size) {
    if (!name || !*name || strchr(name, '/') || !out_path || out_size == 0u) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0u; i < directory_count; ++i) {
        const char *directory = directories[i];
        if (!directory || directory[0] != '/') continue;
        char candidate[PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), "%s/%s",
            directory, name);
        if (written < 0 || (size_t)written >= sizeof(candidate)) continue;
        if (maelys_cli_process_check_executable(candidate, NULL) != 0) continue;
        if ((size_t)written >= out_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(out_path, candidate, (size_t)written + 1u);
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int maelys_cli_executable_directory(
    const char *argv0, char *out_directory, size_t out_size) {
    if (!out_directory || out_size == 0u) {
        errno = EINVAL;
        return -1;
    }
    char resolved[PATH_MAX];
    resolved[0] = '\0';
#if defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t raw_size = (uint32_t)sizeof(raw);
    if (_NSGetExecutablePath(raw, &raw_size) == 0 && !realpath(raw, resolved))
        resolved[0] = '\0';
#elif defined(__linux__)
    ssize_t link_length = readlink("/proc/self/exe", resolved,
        sizeof(resolved) - 1u);
    if (link_length > 0) resolved[link_length] = '\0';
    else resolved[0] = '\0';
#endif
    if (!resolved[0] && argv0 && strchr(argv0, '/') &&
        !realpath(argv0, resolved))
        resolved[0] = '\0';
    if (!resolved[0]) {
        errno = ENOENT;
        return -1;
    }
    char *slash = strrchr(resolved, '/');
    if (!slash) {
        errno = ENOENT;
        return -1;
    }
    if (slash == resolved) slash[1] = '\0';
    else *slash = '\0';
    size_t length = strlen(resolved);
    if (length >= out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out_directory, resolved, length + 1u);
    return 0;
}
