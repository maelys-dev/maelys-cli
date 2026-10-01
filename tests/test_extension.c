#include "check.h"

#include <maelys/cli.h>

#include <errno.h>
#include <limits.h>
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
    char canonical[PATH_MAX];
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
    /* A digest that does not match is a command declared and unusable, not a
     * manifest the loader refuses: one stale extension cost the whole
     * catalog, this dispatcher included, until 0.5.32. */
    CHECK(write_manifest("oci.json", "oci",
        ",\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\""));
    CHECK(maelys_cli_extension_load(path, &extension, &error) == 0);
    CHECK(!extension.digest_verified && extension.unavailable[0] &&
        strstr(extension.unavailable, "sha256") &&
        extension.unavailable_code &&
        strcmp(extension.unavailable_code, "ACCESS_DENIED") == 0);
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
    /* Another cliApi is a command this dispatcher cannot run, declared with
     * UNSUPPORTED: that one really is an absence of function. */
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":2,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) == 0);
    CHECK(extension.unavailable_code &&
        strcmp(extension.unavailable_code, "UNSUPPORTED") == 0 &&
        strstr(extension.unavailable, "cliApi 2"));

    /* An executable that is gone is NOT_FOUND, which an agent may retry. */
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/nonexistent/maelys-x\",\"cliApi\":1,\"version\":\"1\"}",
        0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) == 0);
    CHECK(extension.unavailable_code &&
        strcmp(extension.unavailable_code, "NOT_FOUND") == 0);

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"help\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "VALIDATION_FAILED") == 0);

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "absolute"));

    /* A relative executable stays a refusal: no state of this machine makes
     * that manifest work, and the one above already covers an absent one. */

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 && strstr(error.message, "cliApi"));

    CHECK(write_text(path, "{not json", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0);
    /* A failure at the root names no member: the pointer would be empty. */
    CHECK(!strstr(error.message, " at "));

    /* A failure inside a member names it as an RFC 6901 pointer. */
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"version\":}",
        0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 &&
        strstr(error.message, "not valid JSON at /version:"));

    /* The pointer carries decoded keys, and this document never reached the
     * metadata check: a key with a terminal control drops the pointer. */
    CHECK(write_text(path, "{\"cl\\u001bear\":[1,}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0);
    CHECK(!strchr(error.message, '\033') && !strstr(error.message, " at "));

    /* Duplicate members and invalid UTF-8 are refused by maelys-json. */
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"command\":\"y\",\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 && strstr(error.message, "not valid JSON"));
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"\xff\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0);

    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,\"version\":\"1\","
        "\"summary\":\"clear\\u001b[2J\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 &&
        strstr(error.message, "unsafe"));
    CHECK(write_text(path, "{\"schema\":\"maelys.cli-extension/v1\",\"command\":\"x\","
        "\"executable\":\"/bin/sh\",\"cliApi\":1,"
        "\"version\":\"1\\u202e\"}", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "PROTOCOL_FAILED") == 0 &&
        strstr(error.message, "unsafe"));

    CHECK(write_text(path, "[]", 0644));
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);

    CHECK(write_manifest("bad.json", "x", NULL));
    CHECK(chmod(path, 0666) == 0);
    CHECK(maelys_cli_extension_load(path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "writable"));
    CHECK(unlink(path) == 0);

    /* A symbolic link is followed, as a package manager that links what it
     * installs from its cellar into its prefix needs: the manifest judged is
     * the file it resolves to, in the trusted directory that holds it. */
    char link_path[512];
    (void)snprintf(link_path, sizeof(link_path), "%s/link.json", directory);
    char target[512];
    (void)snprintf(target, sizeof(target), "%s/oci.json", directory);
    CHECK(symlink(target, link_path) == 0);
    CHECK(maelys_cli_extension_load(link_path, &extension, &error) == 0);
    CHECK(strcmp(extension.command, "oci") == 0);
    /* The manifest member keeps the path discovery walked, not the target:
     * a diagnostic names the file an operator installed. */
    CHECK(strcmp(extension.manifest, link_path) == 0);
    /* The modes of the resolved file are what counts, not the link's. */
    CHECK(chmod(target, 0666) == 0);
    CHECK(maelys_cli_extension_load(link_path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 && strstr(error.message, "writable"));
    CHECK(chmod(target, 0644) == 0);
    /* A target whose own directory is open to group or world is refused:
     * modes on the file say who may write these bytes, the directory says
     * who may put other bytes at that path. */
    char elsewhere[512];
    char far_target[512];
    (void)snprintf(elsewhere, sizeof(elsewhere), "%s/untrusted", directory);
    (void)snprintf(far_target, sizeof(far_target), "%s/untrusted/oci.json",
        directory);
    CHECK(mkdir(elsewhere, 0755) == 0);
    CHECK(write_manifest("untrusted/oci.json", "oci", NULL));
    CHECK(unlink(link_path) == 0 && symlink(far_target, link_path) == 0);
    CHECK(maelys_cli_extension_load(link_path, &extension, &error) == 0);
    CHECK(chmod(elsewhere, 0777) == 0);
    CHECK(maelys_cli_extension_load(link_path, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 &&
        strstr(error.message, "directory"));
    /* The fault is at the other end of the link, so the diagnostic names
     * both: the path installed, and the file actually judged. Without it an
     * operator reads a refusal about a directory that is irreproachable. */
    CHECK(strstr(error.message, link_path) && strstr(error.message, far_target));
    CHECK(strstr(error.message, "resolves into a directory"));
    /* The same refusal reaches a manifest that is no link at all, and then
     * names one path and the plain wording. */
    CHECK(maelys_cli_extension_load(far_target, &extension, &error) != 0);
    CHECK(strcmp(error.code, "ACCESS_DENIED") == 0 &&
        strstr(error.message, "directory"));
    CHECK(!strstr(error.message, "resolved to") &&
        strstr(error.message, "is in a directory"));
    CHECK(chmod(elsewhere, 0755) == 0);
    CHECK(unlink(far_target) == 0 && rmdir(elsewhere) == 0);
    /* A dangling link is a missing manifest, not a trusted one. */
    CHECK(unlink(link_path) == 0);
    CHECK(symlink("/nonexistent/maelys.json", link_path) == 0);
    CHECK(maelys_cli_extension_load(link_path, &extension, &error) != 0);
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
    /* A manifest for a command this machine cannot run is discovered too,
     * declared and marked unavailable: hiding it would send an agent to find
     * it by invoking it. */
    char stale[512];
    (void)snprintf(stale, sizeof(stale), "%s/stale.json", directory);
    CHECK(write_text(stale, "{\"schema\":\"maelys.cli-extension/v1\","
        "\"command\":\"stale\",\"executable\":\"/bin/sh\",\"cliApi\":2,"
        "\"version\":\"1\"}", 0644));
    CHECK(maelys_cli_extension_discover(directories, 2u, &set, &error) == 0);
    CHECK(set.count == 4u);
    CHECK(strcmp(set.items[0].command, "alpha") == 0);
    CHECK(strcmp(set.items[1].command, "oci") == 0);
    CHECK(strcmp(set.items[2].command, "stale") == 0 &&
        set.items[2].unavailable[0] && set.items[2].unavailable_code &&
        strcmp(set.items[2].unavailable_code, "UNSUPPORTED") == 0);
    CHECK(strcmp(set.items[3].command, "zeta") == 0);
    CHECK(unlink(stale) == 0);
    maelys_cli_extension_set_clear(&set);
    CHECK(maelys_cli_extension_discover(directories, 2u, &set, &error) == 0);
    CHECK(set.count == 3u);
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
