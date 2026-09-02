cmake_minimum_required(VERSION 3.20)
file(STRINGS VERSION MAELYS_CLI_VERSION LIMIT_COUNT 1)
project(maelys_cli VERSION ${MAELYS_CLI_VERSION} LANGUAGES C)

# CMake entry point for consumers (add_subdirectory, FetchContent or an
# installed package). The Makefile remains the primary build for releases;
# both build the same sources with the same flags.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MAELYS_CLI_BUILD_DISPATCHER "Build the maelys dispatcher" ON)
option(MAELYS_CLI_BUILD_TESTS "Build the maelys-cli tests" OFF)

add_library(maelys_cli STATIC
    src/version.c src/values.c src/environment.c src/files.c src/digest.c
    src/json.c src/terminal.c src/process.c src/catalog.c src/invocation.c
    src/app.c src/extension.c)
add_library(maelys::cli ALIAS maelys_cli)
set_target_properties(maelys_cli PROPERTIES EXPORT_NAME cli OUTPUT_NAME maelys_cli)
target_include_directories(maelys_cli
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(maelys_cli PRIVATE
    _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700 _DEFAULT_SOURCE _DARWIN_C_SOURCE
    MAELYS_CLI_COMMANDS_DIR="${CMAKE_INSTALL_PREFIX}/share/maelys/commands")
target_compile_options(maelys_cli PRIVATE -Wall -Wextra -Wpedantic)

set(MAELYS_CLI_EMBED ${CMAKE_CURRENT_SOURCE_DIR}/tools/maelys-cli-embed)

if(MAELYS_CLI_BUILD_DISPATCHER)
    set(agent_texts ${CMAKE_CURRENT_BINARY_DIR}/generated/agent_texts.c)
    add_custom_command(
        OUTPUT ${agent_texts}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
        COMMAND sh -c "printf 'const char maelys_agents_version[] = \"%s\";\\n' '${MAELYS_CLI_VERSION}' > '${agent_texts}.tmp' && '${MAELYS_CLI_EMBED}' --define VERSION=${MAELYS_CLI_VERSION} maelys_agents_instructions_block=share/agents/instructions-block.md maelys_agents_guide=share/agents/maelys-cli-guide.md maelys_agents_claude_skill=share/agents/claude-skill.md >> '${agent_texts}.tmp' && mv '${agent_texts}.tmp' '${agent_texts}'"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS share/agents/instructions-block.md share/agents/maelys-cli-guide.md
                share/agents/claude-skill.md ${MAELYS_CLI_EMBED} VERSION
        VERBATIM)
    add_executable(maelys cmd/maelys/main.c cmd/maelys/agents.c ${agent_texts})
    target_link_libraries(maelys PRIVATE maelys_cli)
    target_compile_definitions(maelys PRIVATE
        _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700 _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_compile_options(maelys PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MAELYS_CLI_BUILD_TESTS)
    enable_testing()
    foreach(name values json files digest process catalog app extension)
        add_executable(test_${name} tests/test_${name}.c)
        target_link_libraries(test_${name} PRIVATE maelys_cli)
        target_include_directories(test_${name} PRIVATE src)
        target_compile_definitions(test_${name} PRIVATE
            _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700 _DEFAULT_SOURCE _DARWIN_C_SOURCE)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS maelys_cli EXPORT maelys-cli-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/maelys DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(PROGRAMS tools/maelys-cli-embed DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS tools/generate_cli_reference.py
    DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME maelys-cli-reference)
install(DIRECTORY share/agents share/templates
    DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-cli)
install(FILES share/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-cli)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-cli/docs
    FILES_MATCHING PATTERN "*.md" PATTERN "*.json")
install(FILES LICENSE LICENSING.md DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys-cli/docs)
if(MAELYS_CLI_BUILD_DISPATCHER)
    install(TARGETS maelys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_DATADIR}/maelys/commands)
endif()
install(EXPORT maelys-cli-targets
    NAMESPACE maelys::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/maelys-cli)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/maelys-cli-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/maelys-cli-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/maelys-cli
    PATH_VARS CMAKE_INSTALL_BINDIR CMAKE_INSTALL_DATADIR)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/maelys-cli-config-version.cmake
    VERSION ${MAELYS_CLI_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/maelys-cli-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/maelys-cli-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/maelys-cli)
