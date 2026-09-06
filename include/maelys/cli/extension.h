#ifndef MAELYS_CLI_EXTENSION_H
#define MAELYS_CLI_EXTENSION_H

#include "maelys/cli/invocation.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * External command discovery for the `maelys` dispatcher and for products
 * that dispatch external commands. Implemented in libmaelys_cli_extension,
 * a separate archive that reads manifests through maelys-json (pkg-config
 * `maelys-cli-extension`, CMake `maelys::cli_extension`); the core
 * libmaelys_cli has no dependency. Commands are declared by installed
 * manifests, never discovered through PATH:
 *
 * {
 *   "schema": "maelys.cli-extension/v1",
 *   "command": "oci",
 *   "executable": "/opt/homebrew/libexec/maelys/commands/maelys-oci",
 *   "cliApi": 1,
 *   "version": "0.1.0",
 *   "summary": "Manage verified OCI images and artifacts",
 *   "sha256": "optional lowercase digest of the executable"
 * }
 */

#define MAELYS_CLI_EXTENSION_MAX_COMMAND 64u
#define MAELYS_CLI_EXTENSION_MAX_PATH 1024u
#define MAELYS_CLI_EXTENSION_MAX_VERSION 64u
#define MAELYS_CLI_EXTENSION_MAX_SUMMARY 256u
#define MAELYS_CLI_EXTENSION_MAX_MANIFEST_BYTES 65536u
#define MAELYS_CLI_EXTENSION_MAX_EXECUTABLE_BYTES (256u * 1024u * 1024u)

typedef struct maelys_cli_extension {
    char command[MAELYS_CLI_EXTENSION_MAX_COMMAND];
    char executable[MAELYS_CLI_EXTENSION_MAX_PATH];
    char manifest[MAELYS_CLI_EXTENSION_MAX_PATH];
    char version[MAELYS_CLI_EXTENSION_MAX_VERSION];
    char summary[MAELYS_CLI_EXTENSION_MAX_SUMMARY];
    char sha256[65];
    unsigned int cli_api;
    int digest_verified;
} maelys_cli_extension_t;

typedef struct maelys_cli_extension_set {
    maelys_cli_extension_t *items;
    size_t count;
} maelys_cli_extension_set_t;

/* Directories consulted by default, in precedence order. */
const char *const *maelys_cli_extension_default_directories(size_t *out_count);

/* Loads and verifies one manifest: regular non-symlink file with trusted
 * owner and modes, valid schema and cliApi, terminal-safe single-line version
 * and summary, absolute regular trusted executable (stored as its canonical
 * absolute path), optional digest match. */
int maelys_cli_extension_load(
    const char *manifest_path, maelys_cli_extension_t *out,
    maelys_cli_error_t *error);

/* Scans *.json manifests of every directory in lexical order. A command
 * declared twice is an error. Missing directories are skipped. */
int maelys_cli_extension_discover(
    const char *const *directories, size_t directory_count,
    maelys_cli_extension_set_t *out, maelys_cli_error_t *error);

const maelys_cli_extension_t *maelys_cli_extension_find(
    const maelys_cli_extension_set_t *set, const char *command);
void maelys_cli_extension_set_clear(maelys_cli_extension_set_t *set);

#ifdef __cplusplus
}
#endif

#endif
