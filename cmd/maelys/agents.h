#ifndef MAELYS_DISPATCHER_AGENTS_H
#define MAELYS_DISPATCHER_AGENTS_H

#include <maelys/cli.h>

/* Embedded texts generated from the share/agents Markdown files at build time. */
extern const char maelys_agents_version[];
extern const char maelys_agents_instructions_block[];
extern const char maelys_agents_guide[];
extern const char maelys_agents_claude_skill[];

int maelys_agents_install(maelys_cli_context_t *context);
int maelys_agents_status(maelys_cli_context_t *context);

extern const maelys_cli_operand_t maelys_agents_operands[1];
extern const maelys_cli_option_t maelys_agents_install_options[2];
extern const maelys_cli_option_t maelys_agents_status_options[1];
extern const char maelys_agents_install_schema[];
extern const char maelys_agents_status_schema[];

#endif
