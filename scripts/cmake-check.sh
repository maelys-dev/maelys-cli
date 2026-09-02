#!/bin/sh
# Proves the CMake package: build, install into a scratch prefix, then
# configure and build a consumer that uses find_package(maelys-cli).
set -eu
command -v cmake >/dev/null 2>&1 || { echo "cmake-check: skipped (cmake not found)"; exit 0; }
root=$(mktemp -d "${TMPDIR:-/tmp}/maelys-cli-cmake.XXXXXX")
cleanup() { rm -rf "$root"; }
trap cleanup EXIT HUP INT TERM

json_dir=${MAELYS_JSON_DIR:-../maelys-json}
cmake -S "$json_dir" -B "$root/json-build" -DCMAKE_INSTALL_PREFIX="$root/prefix" \
    -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$root/json-build" --parallel >/dev/null
cmake --install "$root/json-build" >/dev/null
cmake -S . -B "$root/build" -DCMAKE_INSTALL_PREFIX="$root/prefix" \
    -DCMAKE_PREFIX_PATH="$root/prefix" \
    -DMAELYS_CLI_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$root/build" --parallel >/dev/null
(cd "$root/build" && ctest --output-on-failure >/dev/null)
cmake --install "$root/build" >/dev/null
test -f "$root/prefix/lib/cmake/maelys-cli/maelys-cli-config.cmake"
test -x "$root/prefix/bin/maelys-cli-embed"
test -x "$root/prefix/bin/maelys-cli-reference"

mkdir -p "$root/consumer"
cat > "$root/consumer/CMakeLists.txt" <<'CONSUMER'
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES C)
find_package(maelys-cli REQUIRED)
add_executable(smoke smoke.c)
target_link_libraries(smoke PRIVATE maelys::cli)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/schema.c
    COMMAND ${MAELYS_CLI_EMBED} smoke_schema=${CMAKE_SOURCE_DIR}/smoke.json > ${CMAKE_BINARY_DIR}/schema.c
    DEPENDS ${CMAKE_SOURCE_DIR}/smoke.json VERBATIM)
target_sources(smoke PRIVATE ${CMAKE_BINARY_DIR}/schema.c)
CONSUMER
printf '{"type":"object"}\n' > "$root/consumer/smoke.json"
cat > "$root/consumer/smoke.c" <<'SMOKE'
#include <maelys/cli.h>
extern const char smoke_schema[];
static int hello(maelys_cli_context_t *context) {
    return maelys_cli_succeed(context, "{}", "hello", MAELYS_CLI_EXIT_OK);
}
static const maelys_cli_command_t commands[] = {
    {MAELYS_CLI_READ("hello", "hello", "Say hello.", hello), MAELYS_CLI_SCHEMA(smoke_schema)},
};
int main(int argc, char **argv) {
    static const maelys_cli_app_t app = {.program = "smoke", .product = "Smoke",
        .version = "0.0.1", .commands = commands, .command_count = 1u};
    return maelys_cli_main(&app, argc, argv);
}
SMOKE
cmake -S "$root/consumer" -B "$root/consumer/build" \
    -DCMAKE_PREFIX_PATH="$root/prefix" >/dev/null
cmake --build "$root/consumer/build" >/dev/null
test "$("$root/consumer/build/smoke" hello)" = "hello"
printf '%s\n' "cmake-check: ok"
