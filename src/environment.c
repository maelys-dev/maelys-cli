#include "maelys/cli/environment.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int valid_name(const char *value, size_t length) {
    if (!value || length == 0u) return 0;
    for (size_t i = 0u; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (i > 0u && c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

int maelys_cli_environment_append(
    maelys_cli_environment_t *environment, const char *assignment) {
    if (!environment || !assignment ||
        environment->count >= SIZE_MAX / sizeof(*environment->entries) - 1u)
        return -1;
    const char *equals = strchr(assignment, '=');
    size_t name_length = equals ? (size_t)(equals - assignment) :
        strlen(assignment);
    if (!valid_name(assignment, name_length) || name_length == SIZE_MAX)
        return -1;
    const char *value = equals ? equals + 1u : getenv(assignment);
    if (!value) return -1;

    char *name_copy = malloc(name_length + 1u);
    char *value_copy = strdup(value);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return -1;
    }
    memcpy(name_copy, assignment, name_length);
    name_copy[name_length] = '\0';

    maelys_cli_environment_entry_t *grown = realloc(
        environment->entries,
        (environment->count + 1u) * sizeof(*environment->entries));
    if (!grown) {
        free(name_copy);
        free(value_copy);
        return -1;
    }
    environment->entries = grown;
    environment->entries[environment->count].name = name_copy;
    environment->entries[environment->count].value = value_copy;
    environment->count++;
    return 0;
}

const char *maelys_cli_environment_get(
    const maelys_cli_environment_t *environment, const char *name) {
    if (!environment || !name) return NULL;
    for (size_t i = environment->count; i > 0u; --i) {
        if (strcmp(environment->entries[i - 1u].name, name) == 0)
            return environment->entries[i - 1u].value;
    }
    return NULL;
}

void maelys_cli_environment_clear(maelys_cli_environment_t *environment) {
    if (!environment) return;
    for (size_t i = 0u; i < environment->count; ++i) {
        free(environment->entries[i].name);
        free(environment->entries[i].value);
    }
    free(environment->entries);
    environment->entries = NULL;
    environment->count = 0u;
}

int maelys_cli_environment_to_envp(
    const maelys_cli_environment_t *environment, char ***out_envp) {
    if (!environment || !out_envp ||
        environment->count >= SIZE_MAX / sizeof(char *) - 1u)
        return -1;
    char **envp = calloc(environment->count + 1u, sizeof(*envp));
    if (!envp) return -1;
    for (size_t i = 0u; i < environment->count; ++i) {
        size_t name_length = strlen(environment->entries[i].name);
        size_t value_length = strlen(environment->entries[i].value);
        if (name_length > SIZE_MAX - value_length - 2u) {
            maelys_cli_envp_free(envp);
            return -1;
        }
        char *joined = malloc(name_length + value_length + 2u);
        if (!joined) {
            maelys_cli_envp_free(envp);
            return -1;
        }
        memcpy(joined, environment->entries[i].name, name_length);
        joined[name_length] = '=';
        memcpy(joined + name_length + 1u, environment->entries[i].value,
            value_length + 1u);
        envp[i] = joined;
    }
    *out_envp = envp;
    return 0;
}

void maelys_cli_envp_free(char **envp) {
    if (!envp) return;
    for (size_t i = 0u; envp[i]; ++i) free(envp[i]);
    free(envp);
}
