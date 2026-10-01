#ifndef MAELYS_CLI_PROCESS_H
#define MAELYS_CLI_PROCESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Safe invocation of external programs: absolute paths only, never a shell,
 * never implicit interpreter lookup (including relative or `env` shebangs),
 * standard descriptors inherited, everything else closed atomically by exec.
 */

typedef struct maelys_cli_process_status {
    int exited;
    int exit_code;
    int signaled;
    int term_signal;
} maelys_cli_process_status_t;

/* Refuses relative paths, symlink targets that are not regular files,
 * binaries writable by group or world, and untrusted immediate parent
 * directories, and scripts whose shebang interpreter is not absolute or is
 * named `env`. The executable and its resolved parent stay open so run and
 * replace execute the object that was checked. Returns -1 with errno. */
int maelys_cli_process_check_executable(
    const char *path, const char **out_error);

/* Runs the program to completion. envp NULL inherits the environment. */
int maelys_cli_process_run(
    const char *path, char *const argv[], char *const envp[],
    maelys_cli_process_status_t *out_status);

/* One descriptor the program inherits: `source` in the caller, `target` in
 * the program, which is where the program expects to find it. The mapping is
 * applied as a whole, so 3 -> 4 beside 4 -> 3 is honoured, and one source may
 * reach several targets. Close-on-exec on the source is irrelevant: the
 * target is installed by dup2, which clears the flag on the descriptor it
 * makes. */
typedef struct maelys_cli_process_inherit {
    int source;
    int target;
} maelys_cli_process_inherit_t;

#define MAELYS_CLI_PROCESS_MAX_INHERIT 16u

/* Zero is the neutral value: no options, or options with no inheritance, is
 * the behaviour of run and replace -- 0, 1 and 2 inherited, everything else
 * closed by exec. */
typedef struct maelys_cli_process_options {
    const maelys_cli_process_inherit_t *inherit;
    size_t inherit_count;
} maelys_cli_process_options_t;

/* A started program: its identity, and the status once waited for. Opaque
 * because the guarantee below is the type's whole purpose. */
typedef struct maelys_cli_process maelys_cli_process_t;

/* Starts the program with the same trust checks as run, and returns once the
 * exec has succeeded -- never before, so a -1 with errno is the failure of
 * this program and never of the one after it. Every inherited descriptor is
 * installed at its target; refused before the fork: a target below 3, two
 * entries naming one target, a source that is not open, more entries than
 * MAELYS_CLI_PROCESS_MAX_INHERIT.
 *
 * The handle holds the program's process id reserved until
 * maelys_cli_process_release: nothing is reaped before then, so the id names
 * this program and no stranger that would otherwise inherit the number. That
 * is what lets signal run in one thread while wait runs in another. */
int maelys_cli_process_start(
    const char *path, char *const argv[], char *const envp[],
    const maelys_cli_process_options_t *options,
    maelys_cli_process_t **out_process);

/* Sends a signal to the program, from any thread and at any time while the
 * handle lives. A program that has already exited is ESRCH, and no signal
 * leaves: the handle is the proof that its id is still its own. */
int maelys_cli_process_signal(
    maelys_cli_process_t *process, int signal_number);

/* Waits for the program to exit and reports how. Called again, it reports
 * the same status without waiting. */
int maelys_cli_process_wait(
    maelys_cli_process_t *process, maelys_cli_process_status_t *out_status);

/* Releases the handle and the process id it held. A program still running at
 * that point keeps running, and the caller owns what is left of it: release
 * after wait is the order every caller wants. */
void maelys_cli_process_release(maelys_cli_process_t *process);

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
