#ifndef MAELYS_CLI_ENVIRONMENT_H
#define MAELYS_CLI_ENVIRONMENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct maelys_cli_environment_entry {
    char *name;
    char *value;
} maelys_cli_environment_entry_t;

/* An owned NAME=VALUE overlay, typically collected from repeated --env. */
typedef struct maelys_cli_environment {
    maelys_cli_environment_entry_t *entries;
    size_t count;
} maelys_cli_environment_t;

/* NAME=VALUE copies the explicit value. NAME alone imports the current
 * process environment and fails when the variable is undefined. Names must
 * match [A-Za-z_][A-Za-z0-9_]*. */
int maelys_cli_environment_append(
    maelys_cli_environment_t *environment, const char *assignment);
const char *maelys_cli_environment_get(
    const maelys_cli_environment_t *environment, const char *name);
void maelys_cli_environment_clear(maelys_cli_environment_t *environment);

/* Builds a NULL-terminated execve-style array. The array and its strings are
 * owned by the caller and released with maelys_cli_envp_free(). */
int maelys_cli_environment_to_envp(
    const maelys_cli_environment_t *environment, char ***out_envp);
void maelys_cli_envp_free(char **envp);

#ifdef __cplusplus
}
#endif

#endif
