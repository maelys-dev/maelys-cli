#ifndef MAELYS_CLI_PROCESS_H
#define MAELYS_CLI_PROCESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Safe invocation of external programs: absolute paths only, never a shell,
 * never PATH lookup, standard descriptors inherited, everything else closed.
 */

typedef struct maelys_cli_process_status {
    int exited;
    int exit_code;
    int signaled;
    int term_signal;
} maelys_cli_process_status_t;

/* Refuses relative paths, symlink targets that are not regular files and
 * binaries writable by group or world. Returns -1 with errno. */
int maelys_cli_process_check_executable(
    const char *path, const char **out_error);

/* Runs the program to completion. envp NULL inherits the environment. */
int maelys_cli_process_run(
    const char *path, char *const argv[], char *const envp[],
    maelys_cli_process_status_t *out_status);

/* Replaces the current process. Returns -1 with errno only on failure. */
int maelys_cli_process_replace(
    const char *path, char *const argv[], char *const envp[]);

/* Conventional shell exit code: exit status, or 128 + signal. */
int maelys_cli_process_exit_code(const maelys_cli_process_status_t *status);

/* Finds NAME as a trusted executable in explicit absolute directories. */
int maelys_cli_process_resolve(
    const char *name, const char *const *directories, size_t directory_count,
    char *out_path, size_t out_size);

/* Directory of the running executable, resolved through the platform
 * facility and falling back to argv0. */
int maelys_cli_executable_directory(
    const char *argv0, char *out_directory, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
