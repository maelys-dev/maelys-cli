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
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1u << 2)
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
#ifndef O_PATH
#define O_PATH 010000000
#endif
#endif

extern char **environ;

typedef struct trusted_executable {
    int descriptor;
    int directory;
    char name[PATH_MAX];
    struct stat status;
} trusted_executable_t;

static void close_trusted_executable(trusted_executable_t *executable) {
    if (executable->descriptor >= 0) (void)close(executable->descriptor);
    if (executable->directory >= 0) (void)close(executable->directory);
    executable->descriptor = -1;
    executable->directory = -1;
}

static int open_trusted_executable(
    const char *path, trusted_executable_t *executable,
    const char **out_error) {
    if (out_error) *out_error = NULL;
    memset(executable, 0, sizeof(*executable));
    executable->descriptor = -1;
    executable->directory = -1;
    if (!path || path[0] != '/') {
        if (out_error) *out_error = "executable path must be absolute";
        errno = EINVAL;
        return -1;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        if (out_error) *out_error = "executable does not exist or is not accessible";
        return -1;
    }
    char *slash = strrchr(resolved, '/');
    if (!slash || !slash[1]) {
        if (out_error) *out_error = "executable path has no file name";
        errno = EINVAL;
        return -1;
    }
    if (strlen(slash + 1) >= sizeof(executable->name)) {
        if (out_error) *out_error = "executable file name is too long";
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(executable->name, slash + 1, strlen(slash + 1) + 1u);
    if (slash == resolved) slash[1] = '\0';
    else *slash = '\0';
    executable->directory = open(resolved,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (executable->directory < 0) {
        if (out_error) *out_error = "executable directory is not accessible";
        return -1;
    }
    struct stat directory_status;
    if (fstat(executable->directory, &directory_status) != 0) {
        if (out_error) *out_error = "executable directory status is not accessible";
        close_trusted_executable(executable);
        return -1;
    }
    if (!S_ISDIR(directory_status.st_mode) ||
        (directory_status.st_uid != 0u && directory_status.st_uid != geteuid()) ||
        (directory_status.st_mode & (S_IWGRP | S_IWOTH))) {
        if (out_error) *out_error = "executable directory is owned or writable by an untrusted user";
        close_trusted_executable(executable);
        errno = EPERM;
        return -1;
    }
#if defined(__linux__)
    const int open_flags = O_PATH | O_CLOEXEC | O_NOFOLLOW;
#elif defined(O_EXEC)
    const int open_flags = O_EXEC | O_CLOEXEC | O_NOFOLLOW;
#else
    const int open_flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
#endif
    executable->descriptor = openat(executable->directory, executable->name,
        open_flags);
    if (executable->descriptor < 0) {
        if (out_error) *out_error = "executable is not accessible without following a symbolic link";
        close_trusted_executable(executable);
        return -1;
    }
    if (fstat(executable->descriptor, &executable->status) != 0) {
        if (out_error) *out_error = "executable status is not accessible";
        close_trusted_executable(executable);
        return -1;
    }
    if (!S_ISREG(executable->status.st_mode)) {
        if (out_error) *out_error = "executable is not a regular file";
        close_trusted_executable(executable);
        errno = EINVAL;
        return -1;
    }
    if (executable->status.st_uid != 0u &&
        executable->status.st_uid != geteuid()) {
        if (out_error) *out_error = "executable is owned by an untrusted user";
        close_trusted_executable(executable);
        errno = EPERM;
        return -1;
    }
    if (executable->status.st_mode & (S_IWGRP | S_IWOTH)) {
        if (out_error) *out_error = "executable is writable by group or world";
        close_trusted_executable(executable);
        errno = EPERM;
        return -1;
    }
    if (!(executable->status.st_mode & S_IXUSR)) {
        if (out_error) *out_error = "executable is not executable";
        close_trusted_executable(executable);
        errno = EACCES;
        return -1;
    }
    return 0;
}

int maelys_cli_process_check_executable(
    const char *path, const char **out_error) {
    trusted_executable_t executable;
    if (open_trusted_executable(path, &executable, out_error) != 0) return -1;
    close_trusted_executable(&executable);
    return 0;
}

/* Keep descriptors available long enough to report an exec failure, but make
 * every non-standard descriptor close atomically when exec succeeds. */
static void cloexec_inherited_descriptors(void) {
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, 3u, ~0u, CLOSE_RANGE_CLOEXEC) == 0) return;
#endif
    struct rlimit limit;
    rlim_t maximum = 0u;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
        maximum = limit.rlim_cur;
    if (maximum == 0u || maximum > (rlim_t)INT_MAX) {
        long configured = sysconf(_SC_OPEN_MAX);
        maximum = configured > 0 ? (rlim_t)configured : (rlim_t)65536u;
    }
    for (int descriptor = 3; (rlim_t)descriptor < maximum; ++descriptor) {
        int flags = fcntl(descriptor, F_GETFD);
        if (flags >= 0 && !(flags & FD_CLOEXEC))
            (void)fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
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

static int execute_trusted(
    const trusted_executable_t *executable, char *const argv[],
    char *const envp[]) {
#if defined(__linux__) && defined(SYS_execveat)
    (void)syscall(SYS_execveat, executable->descriptor, "", argv,
        envp ? envp : environ, AT_EMPTY_PATH);
    /* A script whose held descriptor is close-on-exec returns ENOENT because
     * its interpreter cannot reopen that descriptor. The anchored pathname
     * fallback below executes it without leaking a descriptor. */
    if (errno != ENOENT && errno != ENOSYS && errno != EINVAL) return -1;
#endif
    struct stat current;
    if (fstatat(executable->directory, executable->name, &current,
            AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(current.st_mode) ||
        current.st_dev != executable->status.st_dev ||
        current.st_ino != executable->status.st_ino) {
        errno = ESTALE;
        return -1;
    }
    int previous = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (previous < 0 || fchdir(executable->directory) != 0) {
        int saved = errno;
        if (previous >= 0) (void)close(previous);
        errno = saved;
        return -1;
    }
    execve(executable->name, argv, envp ? envp : environ);
    int saved = errno;
    (void)fchdir(previous);
    (void)close(previous);
    errno = saved;
    return -1;
}

int maelys_cli_process_run(
    const char *path, char *const argv[], char *const envp[],
    maelys_cli_process_status_t *out_status) {
    if (!path || !argv || !out_status) {
        errno = EINVAL;
        return -1;
    }
    memset(out_status, 0, sizeof(*out_status));
    trusted_executable_t executable;
    if (open_trusted_executable(path, &executable, NULL) != 0) return -1;
    int error_pipe[2];
    if (pipe(error_pipe) != 0) {
        close_trusted_executable(&executable);
        return -1;
    }
    if (fcntl(error_pipe[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        int saved = errno;
        (void)close(error_pipe[0]);
        (void)close(error_pipe[1]);
        close_trusted_executable(&executable);
        errno = saved;
        return -1;
    }
    (void)fflush(stdout);
    (void)fflush(stderr);
    pid_t child = fork();
    if (child < 0) {
        int saved = errno;
        (void)close(error_pipe[0]);
        (void)close(error_pipe[1]);
        close_trusted_executable(&executable);
        errno = saved;
        return -1;
    }
    if (child == 0) {
        (void)close(error_pipe[0]);
        reset_signals();
        cloexec_inherited_descriptors();
        (void)execute_trusted(&executable, argv, envp);
        int saved = errno;
        ssize_t reported;
        do {
            reported = write(error_pipe[1], &saved, sizeof(saved));
        } while (reported < 0 && errno == EINTR);
        _exit(reported == (ssize_t)sizeof(saved) ? 127 : 126);
    }
    (void)close(error_pipe[1]);
    close_trusted_executable(&executable);
    int failure = 0;
    ssize_t amount;
    do {
        amount = read(error_pipe[0], &failure, sizeof(failure));
    } while (amount < 0 && errno == EINTR);
    int read_error = amount < 0 ? errno : 0;
    (void)close(error_pipe[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) return -1;
    if (read_error) {
        errno = read_error;
        return -1;
    }
    if (amount != 0 && amount != (ssize_t)sizeof(failure)) {
        errno = EIO;
        return -1;
    }
    if (amount == (ssize_t)sizeof(failure)) {
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
    trusted_executable_t executable;
    if (open_trusted_executable(path, &executable, NULL) != 0) return -1;
    (void)fflush(stdout);
    (void)fflush(stderr);
    cloexec_inherited_descriptors();
    (void)execute_trusted(&executable, argv, envp);
    int saved = errno;
    close_trusted_executable(&executable);
    errno = saved;
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
