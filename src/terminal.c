#include "maelys/cli/terminal.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static unsigned int detect_columns(int descriptor) {
    const char *columns = getenv("COLUMNS");
    if (columns && *columns) {
        char *end = NULL;
        unsigned long parsed = strtoul(columns, &end, 10);
        if (end && !*end && parsed > 0ul && parsed < 10000ul)
            return (unsigned int)parsed;
    }
#ifdef TIOCGWINSZ
    struct winsize size;
    if (ioctl(descriptor, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
        return size.ws_col;
#endif
    return 80u;
}

void maelys_cli_terminal_detect(
    maelys_cli_terminal_t *terminal, maelys_cli_color_mode_t mode) {
    if (!terminal) return;
    memset(terminal, 0, sizeof(*terminal));
    terminal->stdout_is_tty = isatty(STDOUT_FILENO) == 1;
    terminal->stderr_is_tty = isatty(STDERR_FILENO) == 1;
    terminal->columns = detect_columns(
        terminal->stdout_is_tty ? STDOUT_FILENO : STDERR_FILENO);
    if (mode == MAELYS_CLI_COLOR_NEVER) return;
    const char *force = getenv("CLICOLOR_FORCE");
    const char *no_color = getenv("NO_COLOR");
    const char *term = getenv("TERM");
    int forced = mode == MAELYS_CLI_COLOR_ALWAYS ||
        (force && *force && strcmp(force, "0") != 0);
    if (forced) {
        terminal->color_stdout = 1;
        terminal->color_stderr = 1;
        return;
    }
    if ((no_color && *no_color) || !term || !*term || !strcmp(term, "dumb"))
        return;
    terminal->color_stdout = terminal->stdout_is_tty;
    terminal->color_stderr = terminal->stderr_is_tty;
}

const char *maelys_cli_style(int enabled, maelys_cli_style_t style) {
    if (!enabled) return "";
    switch (style) {
        case MAELYS_CLI_STYLE_BOLD: return "\033[1m";
        case MAELYS_CLI_STYLE_DIM: return "\033[2m";
        case MAELYS_CLI_STYLE_ERROR: return "\033[31m";
        case MAELYS_CLI_STYLE_WARNING: return "\033[33m";
        case MAELYS_CLI_STYLE_SUCCESS: return "\033[32m";
        case MAELYS_CLI_STYLE_INFO: return "\033[36m";
        case MAELYS_CLI_STYLE_RESET: return "\033[0m";
    }
    return "";
}
