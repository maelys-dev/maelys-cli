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
option(MAELYS_CLI_BUILD_EXTENSION
    "Build libmaelys_cli_extension (manifest discovery, needs maelys-json)" ON)
option(MAELYS_CLI_BUILD_TESTS "Build the maelys-cli tests" OFF)
if(MAELYS_CLI_BUILD_DISPATCHER AND NOT MAELYS_CLI_BUILD_EXTENSION)
    message(FATAL_ERROR "the dispatcher requires MAELYS_CLI_BUILD_EXTENSION")
endif()

# The core has no dependency. The extension archive (manifest discovery)
# and the dispatcher read untrusted JSON through maelys-json: an installed
# package, or a sibling source tree for in-tree consumers; without either
# they are skipped and the core still builds and installs.
set(MAELYS_JSON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../maelys-json"
    CACHE PATH "Path to the maelys-json source tree (fallback)")
set(MAELYS_CLI_HAVE_JSON OFF)
set(MAELYS_CLI_JSON_INSTALLED OFF)
if(MAELYS_CLI_BUILD_EXTENSION)
    find_package(maelys-json 0.1 CONFIG QUIET)
endif()
if(NOT MAELYS_CLI_BUILD_EXTENSION)
    message(STATUS "maelys-cli: core only, maelys-json not configured")
elseif(maelys-json_FOUND)
    set(MAELYS_CLI_HAVE_JSON ON)
    set(MAELYS_CLI_JSON_INSTALLED ON)
elseif(EXISTS "${MAELYS_JSON_SOURCE_DIR}/CMakeLists.txt")
    set(MAELYS_CLI_HAVE_JSON ON)
    add_subdirectory("${MAELYS_JSON_SOURCE_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/maelys-json" EXCLUDE_FROM_ALL)
else()
    message(STATUS "maelys-json not found: extension archive and dispatcher skipped")
endif()

add_library(maelys_cli STATIC
    src/version.c src/values.c src/environment.c src/files.c src/digest.c
    src/json.c src/terminal.c src/process.c src/catalog.c src/invocation.c
    src/app.c)
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

if(MAELYS_CLI_HAVE_JSON)
    add_library(maelys_cli_extension STATIC src/extension.c)
    add_library(maelys::cli_extension ALIAS maelys_cli_extension)
    set_target_properties(maelys_cli_extension PROPERTIES
        EXPORT_NAME cli_extension OUTPUT_NAME maelys_cli_extension)
    target_include_directories(maelys_cli_extension
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(maelys_cli_extension PRIVATE
        _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700 _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_compile_options(maelys_cli_extension PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(maelys_cli_extension PUBLIC maelys_cli maelys::json)
endif()

set(MAELYS_CLI_EMBED ${CMAKE_CURRENT_SOURCE_DIR}/tools/maelys-cli-embed)

if(MAELYS_CLI_BUILD_DISPATCHER AND MAELYS_CLI_HAVE_JSON)
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
    target_link_libraries(maelys PRIVATE maelys_cli_extension)
    target_compile_definitions(maelys PRIVATE
        _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700 _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_compile_options(maelys PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MAELYS_CLI_BUILD_TESTS)
    enable_testing()
    set(test_names values json files digest process catalog app)
    if(MAELYS_CLI_HAVE_JSON)
        list(APPEND test_names extension)
    endif()
    foreach(name ${test_names})
        add_executable(test_${name} tests/test_${name}.c)
        target_link_libraries(test_${name} PRIVATE maelys_cli)
        if(name STREQUAL "extension")
            target_link_libraries(test_${name} PRIVATE maelys_cli_extension)
        endif()
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
if(MAELYS_CLI_JSON_INSTALLED)
    install(TARGETS maelys_cli_extension EXPORT maelys-cli-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
elseif(MAELYS_CLI_HAVE_JSON)
    message(STATUS "maelys-json taken from source: maelys::cli_extension is not installed")
endif()
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
if(MAELYS_CLI_BUILD_DISPATCHER AND MAELYS_CLI_HAVE_JSON)
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
