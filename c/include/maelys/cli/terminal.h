#ifndef MAELYS_CLI_TERMINAL_H
#define MAELYS_CLI_TERMINAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum maelys_cli_color_mode {
    MAELYS_CLI_COLOR_AUTO = 0,
    MAELYS_CLI_COLOR_ALWAYS = 1,
    MAELYS_CLI_COLOR_NEVER = 2
} maelys_cli_color_mode_t;

typedef struct maelys_cli_terminal {
    int stdout_is_tty;
    int stderr_is_tty;
    int color_stdout;
    int color_stderr;
    unsigned int columns;
} maelys_cli_terminal_t;

/* Applies, in order: the explicit mode, CLICOLOR_FORCE, NO_COLOR, TERM=dumb
 * and finally isatty() per stream. Columns come from COLUMNS or the tty. */
void maelys_cli_terminal_detect(
    maelys_cli_terminal_t *terminal, maelys_cli_color_mode_t mode);

typedef enum maelys_cli_style {
    MAELYS_CLI_STYLE_RESET = 0,
    MAELYS_CLI_STYLE_BOLD,
    MAELYS_CLI_STYLE_DIM,
    MAELYS_CLI_STYLE_ERROR,
    MAELYS_CLI_STYLE_WARNING,
    MAELYS_CLI_STYLE_SUCCESS,
    MAELYS_CLI_STYLE_INFO
} maelys_cli_style_t;

/* Returns an ANSI sequence when enabled, otherwise the empty string. */
const char *maelys_cli_style(int enabled, maelys_cli_style_t style);

#ifdef __cplusplus
}
#endif

#endif
