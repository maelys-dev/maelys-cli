#ifndef MAELYS_CLI_VERSION_H
#define MAELYS_CLI_VERSION_H

#define MAELYS_CLI_VERSION "0.1.0"
#define MAELYS_CLI_VERSION_MAJOR 0
#define MAELYS_CLI_VERSION_MINOR 1
#define MAELYS_CLI_VERSION_PATCH 0

/* Link-level ABI generation of libmaelys_cli. */
#define MAELYS_CLI_ABI 1

/* Machine-readable envelope contract shared with Maelys Git and Hermes. */
#define MAELYS_CLI_CONTRACT "agent-cli/v2"
#define MAELYS_CLI_SCHEMA_VERSION 2

/* Contract between the `maelys` dispatcher and external command processes. */
#define MAELYS_CLI_API 1
#define MAELYS_CLI_EXTENSION_SCHEMA "maelys.cli-extension/v1"

#ifdef __cplusplus
extern "C" {
#endif

const char *maelys_cli_version(void);

#ifdef __cplusplus
}
#endif

#endif
