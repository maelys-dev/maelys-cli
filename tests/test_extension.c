#include "check.h"

#include <maelys/cli.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[] = "/tmp/maelys-cli-extension.XXXXXX";
static char executable[512];

static int write_text(const char *path, const char *text, mode_t mode) {
    return maelys_cli_write_file_atomic(path, text, strlen(text), mode,
        MAELYS_CLI_WRITE_REPLACE) == 0;
}

static int write_manifest(const char *name, const char *command, const char *extra) {
    char path[512];
    char text[1024];
    (void)snprintf(path, sizeof(path), "%s/%s", directory, name);
    (void)snprintf(text, sizeof(text),
        "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"%s\","
        "\"executable\":\"%s\",\"cliApi\":1,\"version\":\"0.1.0\","
        "\"summary\":\"Test command\"%s}", command, executable, extra ? extra : "");
    return write_text(path, text, 0644);
}

static int test_valid_manifest(void) {
    CHECK(write_manifest("oci.json", "oci", NULL));
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/oci.json", directory);
    maelys_cli_extension_t extension;
    maelys_cli_error_t error;
    CHECK(maelys_cli_extension_load(path, &extension, &error) == 0);
    CHECK(strcmp(extension.command, "oci") == 0 && extension.cli_api == 1u);
    char canonical[512];
    CHECK(realpath(executable, canonical) != NULL);
    CHECK(strcmp(extension.executable, canonical) == 0 && !extension.digest_verified);
    CHECK(strcmp(extension.summary, "Test command") == 0);
    char target[512];
    char link[512];
    memcpy(target, canonical, strlen(canonical) + 1u);
    (void)snprintf(link, sizeof(link), "%s/executable-link", directory);
    CHECK(symlink(target, link) == 0);
    memcpy(executable, link, strlen(link) + 1u);
    CHECK(write_manifest("oci.json", "oci", NULL));
    CHECK(maelys_cli_extension_load(path, &extension, &error) == 0);
    CHECK(strcmp(extension.executable, target) == 0);
    CHECK(unlink(link) == 0);
    memcpy(executable, target, strlen(target) + 1u);
    char digest_extra[128];
    char hex[MAELYS_CLI_SHA256_HEX_SIZE];
    CHECK(maelys_cli_sha256_file(executable, 1u << 20, hex) == 0);
    (void)snprintf(digest_extra, sizeof(digest_extra), ",\"sha256\":\"%s\"", hex);
    CHECK(write_manifest("oci.json", "oci", digest_extra));
    CHECK(maelys_cli_extension_load(path, &extension, &error) == 0 && extension.digest_verified);
    CHECK(write_manifest("oci.json", "oci",
        ",\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\""));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "sha256"));
    CHECK(write_manifest("oci.json", "oci", NULL));
    return 1;
}

static int test_rejections(void) {
    char path[512];
    maelys_cli_extension_t extension;
    maelys_cli_error_t error;
    (void)snprintf(path, sizeof(path), "%s/bad.json", directory);

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v2\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "UNSUPPORTED") == 0);

    CHECK(write_manifest("bad.json", "x", ",\"cliApi\":2"));
    /* Duplicate key: last one wins in our lookup? No: first match wins. Use a
     * distinct manifest instead. */
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":2,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "UNSUPPORTED") == 0 && strstr(error.message, "cliApi 2"));

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"help\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "VALIDATION_FAILED") == 0);

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "absolute"));

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/nonexistent/maelys-x\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0);

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 && strstr(error.message, "cliApi"));

    CHECK(write_text(path, "{not json", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0);

    /* Duplicate members and invalid UTF-8 are refused by maelys-json. */
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"command\":\"y\",\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 && strstr(error.message, "not valid JSON"));
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"\xff\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0);

    CHECK(write_text(path, "[]", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);

    CHECK(write_manifest("bad.json", "x", NULL));
    CHECK(chmod(path, 0666) == 0);
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "writable"));
    CHECK(unlink(path) == 0);

    char link_path[512];
    (void)snprintf(link_path, sizeof(link_path), "%s/link.json", directory);
    char target[512];
    (void)snprintf(target, sizeof(target), "%s/oci.json", directory);
    CHECK(symlink(target, link_path) == 0);
    CHECK(maelys_cli_extension_load(link_path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "symbolic"));
    CHECK(unlink(link_path) == 0);

    CHECK(maelys_cli_extension_load("relative.json", &extension, &error) != 0);
    CHECK(maelys_cli_extension_load("/nonexistent/maelys.json", &extension, &error) != 0);
    return 1;
}

static int test_discover(void) {
    maelys_cli_extension_set_t set;
    maelys_cli_error_t error;
    const char *directories[] = {directory, "/nonexistent/maelys/commands"};
    CHECK(write_manifest("zeta.json", "zeta", NULL));
    CHECK(write_manifest("alpha.json", "alpha", NULL));
    char ignored[512];
    (void)snprintf(ignored, sizeof(ignored), "%s/README.txt", directory);
    CHECK(write_text(ignored, "ignored", 0644));
    CHECK(maelys_cli_extension_discover(directories, 2u, &set, &error) == 0);
    CHECK(set.count == 3u);
    CHECK(strcmp(set.items[0].command, "alpha") == 0);
    CHECK(strcmp(set.items[1].command, "oci") == 0);
    CHECK(strcmp(set.items[2].command, "zeta") == 0);
    CHECK(maelys_cli_extension_find(&set, "oci") == &set.items[1]);
    CHECK(maelys_cli_extension_find(&set, "nope") == NULL);
    maelys_cli_extension_set_clear(&set);
    CHECK(set.items == NULL && set.count == 0u);

    CHECK(write_manifest("dup.json", "oci", NULL));
    CHECK(maelys_cli_extension_discover(directories, 1u, &set, &error) != 0);
    CHECK(strstr(error.message, "declared by both") != NULL && set.count == 0u);
    char dup[512];
    (void)snprintf(dup, sizeof(dup), "%s/dup.json", directory);
    CHECK(unlink(dup) == 0);

    const char *relative[] = {"relative"};
    CHECK(maelys_cli_extension_discover(relative, 1u, &set, &error) != 0);

    size_t count = 0u;
    const char *const *defaults = maelys_cli_extension_default_directories(&count);
    CHECK(count >= 3u && defaults[count - 1u][0] == '/');
    (void)unlink(ignored);
    return 1;
}

int main(void) {
    if (!mkdtemp(directory)) return 1;
    (void)snprintf(executable, sizeof(executable), "%s/maelys-test-command", directory);
    if (!write_text(executable, "#!/bin/sh\nexit 0\n", 0755)) return 1;
    int failures = 0;
    RUN(test_valid_manifest);
    RUN(test_rejections);
    RUN(test_discover);
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s/oci.json", directory);
    (void)unlink(pattern);
    (void)snprintf(pattern, sizeof(pattern), "%s/zeta.json", directory);
    (void)unlink(pattern);
    (void)snprintf(pattern, sizeof(pattern), "%s/alpha.json", directory);
    (void)unlink(pattern);
    (void)unlink(executable);
    (void)rmdir(directory);
    return failures ? 1 : 0;
}
