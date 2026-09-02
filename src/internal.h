#ifndef MAELYS_CLI_INTERNAL_H
#define MAELYS_CLI_INTERNAL_H

#include "maelys/cli/app.h"
#include "maelys/cli/catalog.h"
#include "maelys/cli/invocation.h"

#include <stddef.h>

/* Transport options accepted by every command with an envelope output. */
const maelys_cli_option_t *maelys_cli_transport_options(size_t *out_count);

/* Iterates built-in commands followed by the product catalog. */
size_t maelys_cli_app_command_count(const maelys_cli_app_t *app);
const maelys_cli_command_t *maelys_cli_app_command_at(
    const maelys_cli_app_t *app, size_t index);
const maelys_cli_command_t *maelys_cli_app_find_command(
    const maelys_cli_app_t *app, const char *id);

size_t maelys_cli_pattern_words(const char *pattern);

/* Validates text against an option's kind; fills the typed value. */
int maelys_cli_option_validate_text(
    const maelys_cli_option_t *option, const char *value,
    maelys_cli_parsed_option_t *parsed, maelys_cli_error_t *error);

/* Set by maelys_cli_main() so delegates can be resolved beside the binary. */
extern const char *maelys_cli_argv0;

#endif
