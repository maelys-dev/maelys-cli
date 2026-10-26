#include "maelys/cli.h"

int main() {
    maelys_cli_option_t option{};
    option.kind = MAELYS_CLI_VALUE_SIZE;
    maelys_cli_command_t command{};
    command.effect = MAELYS_CLI_EFFECT_READ;
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    maelys_cli_json_writer_clear(&writer);
    return option.kind == MAELYS_CLI_VALUE_SIZE &&
        command.effect == MAELYS_CLI_EFFECT_READ &&
        maelys_cli_version() != nullptr ? 0 : 1;
}
