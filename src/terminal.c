#include "maelys/cli/terminal.h"
#include "internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static size_t decode_utf8(const unsigned char *text, uint32_t *out) {
    if (text[0] < 0x80u) {
        *out = text[0];
        return 1u;
    }
    if (text[0] >= 0xc2u && text[0] <= 0xdfu &&
        text[1] >= 0x80u && text[1] <= 0xbfu) {
        *out = ((uint32_t)(text[0] & 0x1fu) << 6u) |
            (uint32_t)(text[1] & 0x3fu);
        return 2u;
    }
    if (text[0] >= 0xe0u && text[0] <= 0xefu && text[1] && text[2] &&
        text[1] >= (text[0] == 0xe0u ? 0xa0u : 0x80u) &&
        text[1] <= (text[0] == 0xedu ? 0x9fu : 0xbfu) &&
        text[2] >= 0x80u && text[2] <= 0xbfu) {
        *out = ((uint32_t)(text[0] & 0x0fu) << 12u) |
            ((uint32_t)(text[1] & 0x3fu) << 6u) |
            (uint32_t)(text[2] & 0x3fu);
        return 3u;
    }
    if (text[0] >= 0xf0u && text[0] <= 0xf4u && text[1] && text[2] &&
        text[3] && text[1] >= (text[0] == 0xf0u ? 0x90u : 0x80u) &&
        text[1] <= (text[0] == 0xf4u ? 0x8fu : 0xbfu) &&
        text[2] >= 0x80u && text[2] <= 0xbfu &&
        text[3] >= 0x80u && text[3] <= 0xbfu) {
        *out = ((uint32_t)(text[0] & 0x07u) << 18u) |
            ((uint32_t)(text[1] & 0x3fu) << 12u) |
            ((uint32_t)(text[2] & 0x3fu) << 6u) |
            (uint32_t)(text[3] & 0x3fu);
        return 4u;
    }
    return 0u;
}

void maelys_cli_fprint_terminal_safe(FILE *stream, const char *text) {
    if (!stream || !text) return;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        if (*cursor == '\n' || *cursor == '\r' || *cursor == '\t') {
            (void)fputs(*cursor == '\n' ? "\\n" :
                *cursor == '\r' ? "\\r" : "\\t", stream);
            ++cursor;
            continue;
        }
        uint32_t codepoint = 0u;
        size_t length = decode_utf8(cursor, &codepoint);
        if (length == 0u) {
            (void)fprintf(stream, "\\x%02x", (unsigned int)*cursor++);
        } else if (codepoint < 0x20u ||
                   (codepoint >= 0x7fu && codepoint <= 0x9fu)) {
            (void)fprintf(stream, "\\x%02x", (unsigned int)codepoint);
            cursor += length;
        } else if (codepoint == 0x2028u || codepoint == 0x2029u ||
                   (codepoint >= 0x202au && codepoint <= 0x202eu) ||
                   (codepoint >= 0x2066u && codepoint <= 0x2069u)) {
            (void)fprintf(stream, "\\u%04x", (unsigned int)codepoint);
            cursor += length;
        } else {
            (void)fwrite(cursor, 1u, length, stream);
            cursor += length;
        }
    }
}

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
