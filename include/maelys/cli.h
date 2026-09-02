#ifndef MAELYS_CLI_H
#define MAELYS_CLI_H

/*
 * Umbrella header for libmaelys_cli, the shared Maelys command-line
 * framework. Every module is usable on its own; include the module header
 * directly when a program only needs one layer.
 *
 * Namespace: every identifier starting with maelys_cli_ or MAELYS_CLI_ is
 * reserved for the framework. A product names its own catalog, handlers and
 * helpers with its own prefix (maelys_git_cli_, maelys_egress_cli_, ...).
 */

#include "maelys/cli/version.h"
#include "maelys/cli/values.h"
#include "maelys/cli/environment.h"
#include "maelys/cli/files.h"
#include "maelys/cli/digest.h"
#include "maelys/cli/json.h"
#include "maelys/cli/terminal.h"
#include "maelys/cli/process.h"
#include "maelys/cli/catalog.h"
#include "maelys/cli/invocation.h"
#include "maelys/cli/app.h"
#include "maelys/cli/extension.h"

#endif
