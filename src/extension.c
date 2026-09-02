#include "maelys/cli/extension.h"
#include "maelys/cli/digest.h"
#include "maelys/cli/files.h"
#include "maelys/cli/json.h"
#include "maelys/cli/process.h"
#include "maelys/cli/version.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const default_directories[] = {
#ifdef MAELYS_CLI_COMMANDS_DIR
    MAELYS_CLI_COMMANDS_DIR,
#endif
    "/opt/homebrew/share/maelys/commands",
    "/usr/local/share/maelys/commands",
    "/usr/share/maelys/commands",
};

const char *const *maelys_cli_extension_default_directories(size_t *out_count) {
    if (out_count)
        *out_count = sizeof(default_directories) / sizeof(default_directories[0]);
    return default_directories;
}

static int valid_command_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0u || length >= MAELYS_CLI_EXTENSION_MAX_COMMAND) return 0;
    if (name[0] < 'a' || name[0] > 'z') return 0;
    for (const char *p = name; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-'))
            return 0;
    }
    return strcmp(name, "help") && strcmp(name, "version") &&
        strcmp(name, "describe");
}

static int copy_string_field(
    const char *text, const char *key, int required, char *out,
    size_t out_size, const char *manifest, maelys_cli_error_t *error) {
    const char *value = NULL;
    size_t length = 0u;
    int found = maelys_cli_json_object_get(text, key, &value, &length);
    if (found < 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Repair or remove the manifest.",
            "Manifest %s is not a JSON object.", manifest);
        return -1;
    }
    if (found == 0) {
        if (!required) {
            out[0] = '\0';
            return 0;
        }
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Add the required manifest member.",
            "Manifest %s lacks '%s'.", manifest, key);
        return -1;
    }
    char *decoded = NULL;
    if (maelys_cli_json_decode_string(value, length, &decoded) != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Use a JSON string for this member.",
            "Manifest %s member '%s' is not a string.", manifest, key);
        return -1;
    }
    size_t decoded_length = strlen(decoded);
    if (decoded_length >= out_size) {
        free(decoded);
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Shorten the manifest member.",
            "Manifest %s member '%s' is too long.", manifest, key);
        return -1;
    }
    memcpy(out, decoded, decoded_length + 1u);
    free(decoded);
    return 0;
}

int maelys_cli_extension_load(
    const char *manifest_path, maelys_cli_extension_t *out,
    maelys_cli_error_t *error) {
    if (!manifest_path || !out) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Invalid extension loader arguments.");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (manifest_path[0] != '/' ||
        strlen(manifest_path) >= sizeof(out->manifest)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use an absolute manifest path.",
            "Manifest path %s is not absolute or is too long.", manifest_path);
        return -1;
    }
    memcpy(out->manifest, manifest_path, strlen(manifest_path) + 1u);
    const char *explanation = NULL;
    if (maelys_cli_check_file(manifest_path,
            MAELYS_CLI_FILE_REGULAR | MAELYS_CLI_FILE_NO_SYMLINK |
            MAELYS_CLI_FILE_OWNER_TRUSTED |
            MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS, &explanation) != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_ACCESS_DENIED,
            "Install manifests as regular files owned by root or the current "
            "user, not writable by group or world.",
            "Manifest %s is untrusted: %s.", manifest_path,
            explanation ? explanation : strerror(errno));
        return -1;
    }
    unsigned char *bytes = NULL;
    size_t size = 0u;
    if (maelys_cli_read_regular_file(manifest_path, 2u,
            MAELYS_CLI_EXTENSION_MAX_MANIFEST_BYTES, &bytes, &size) != 0) {
        maelys_cli_error_from_errno(error, MAELYS_CLI_CODE_IO_FAILED, errno,
            manifest_path);
        return -1;
    }
    char *text = malloc(size + 1u);
    if (!text) {
        free(bytes);
        maelys_cli_error_from_errno(error, MAELYS_CLI_CODE_UNEXPECTED, ENOMEM,
            manifest_path);
        return -1;
    }
    memcpy(text, bytes, size);
    text[size] = '\0';
    free(bytes);
    if (memchr(text, '\0', size) != NULL ||
        maelys_cli_json_validate(text, size, NULL) != 0) {
        free(text);
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Repair or remove the manifest.",
            "Manifest %s is not valid JSON.", manifest_path);
        return -1;
    }
    char schema[64];
    int result = -1;
    if (copy_string_field(text, "schema", 1, schema, sizeof(schema),
            manifest_path, error) != 0)
        goto done;
    if (strcmp(schema, MAELYS_CLI_EXTENSION_SCHEMA) != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNSUPPORTED,
            "Reinstall the extension for this dispatcher version.",
            "Manifest %s declares unsupported schema %s.", manifest_path, schema);
        goto done;
    }
    if (copy_string_field(text, "command", 1, out->command,
            sizeof(out->command), manifest_path, error) != 0 ||
        copy_string_field(text, "executable", 1, out->executable,
            sizeof(out->executable), manifest_path, error) != 0 ||
        copy_string_field(text, "version", 1, out->version,
            sizeof(out->version), manifest_path, error) != 0 ||
        copy_string_field(text, "summary", 0, out->summary,
            sizeof(out->summary), manifest_path, error) != 0 ||
        copy_string_field(text, "sha256", 0, out->sha256,
            sizeof(out->sha256), manifest_path, error) != 0)
        goto done;
    if (!valid_command_name(out->command)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use a lowercase command name that is not a built-in.",
            "Manifest %s declares invalid command '%s'.", manifest_path,
            out->command);
        goto done;
    }
    const char *api_value = NULL;
    size_t api_length = 0u;
    uint64_t api = 0u;
    if (maelys_cli_json_object_get(text, "cliApi", &api_value, &api_length) != 1 ||
        maelys_cli_json_decode_unsigned(api_value, api_length, &api) != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Declare cliApi as an unsigned integer.",
            "Manifest %s lacks a valid 'cliApi'.", manifest_path);
        goto done;
    }
    if (api != MAELYS_CLI_API) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNSUPPORTED,
            "Reinstall the extension for this dispatcher version.",
            "Manifest %s requires cliApi %llu; this dispatcher provides %d.",
            manifest_path, (unsigned long long)api, MAELYS_CLI_API);
        goto done;
    }
    out->cli_api = (unsigned int)api;
    if (maelys_cli_process_check_executable(out->executable, &explanation) != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_ACCESS_DENIED,
            "Install the executable as an absolute, regular, trusted binary.",
            "Executable %s of manifest %s is unusable: %s.", out->executable,
            manifest_path, explanation ? explanation : strerror(errno));
        goto done;
    }
    if (out->sha256[0]) {
        char actual[MAELYS_CLI_SHA256_HEX_SIZE];
        if (strlen(out->sha256) != 64u ||
            maelys_cli_sha256_file(out->executable,
                MAELYS_CLI_EXTENSION_MAX_EXECUTABLE_BYTES, actual) != 0 ||
            strcmp(actual, out->sha256) != 0) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_ACCESS_DENIED,
                "Reinstall the extension; its binary does not match the "
                "manifest digest.",
                "Executable %s does not match the sha256 declared in %s.",
                out->executable, manifest_path);
            goto done;
        }
        out->digest_verified = 1;
    }
    result = 0;
done:
    free(text);
    return result;
}

static int compare_names(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static int append_extension(
    maelys_cli_extension_set_t *set, const maelys_cli_extension_t *extension) {
    if (set->count >= SIZE_MAX / sizeof(*set->items) - 1u) return -1;
    maelys_cli_extension_t *grown = realloc(set->items,
        (set->count + 1u) * sizeof(*set->items));
    if (!grown) return -1;
    set->items = grown;
    set->items[set->count++] = *extension;
    return 0;
}

static int discover_directory(
    const char *directory, maelys_cli_extension_set_t *set,
    maelys_cli_error_t *error) {
    DIR *handle = opendir(directory);
    if (!handle) {
        if (errno == ENOENT || errno == ENOTDIR) return 0;
        maelys_cli_error_from_errno(error, MAELYS_CLI_CODE_IO_FAILED, errno,
            directory);
        return -1;
    }
    char **names = NULL;
    size_t count = 0u;
    int result = 0;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        const char *name = entry->d_name;
        size_t length = strlen(name);
        if (name[0] == '.' || length < 6u || strcmp(name + length - 5u, ".json"))
            continue;
        char **grown = realloc(names, (count + 1u) * sizeof(*names));
        char *copy = strdup(name);
        if (!grown || !copy) {
            free(copy);
            if (grown) names = grown;
            result = -1;
            maelys_cli_error_from_errno(error, MAELYS_CLI_CODE_UNEXPECTED,
                ENOMEM, directory);
            break;
        }
        names = grown;
        names[count++] = copy;
    }
    (void)closedir(handle);
    if (result == 0 && count > 0u) qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0u; result == 0 && i < count; ++i) {
        char path[MAELYS_CLI_EXTENSION_MAX_PATH];
        int written = snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Shorten the manifest path.",
                "Manifest path in %s is too long.", directory);
            result = -1;
            break;
        }
        maelys_cli_extension_t extension;
        if (maelys_cli_extension_load(path, &extension, error) != 0) {
            result = -1;
            break;
        }
        const maelys_cli_extension_t *existing = maelys_cli_extension_find(
            set, extension.command);
        if (existing) {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Remove one of the manifests declaring the same command.",
                "Command '%s' is declared by both %s and %s.",
                extension.command, existing->manifest, path);
            result = -1;
            break;
        }
        if (append_extension(set, &extension) != 0) {
            maelys_cli_error_from_errno(error, MAELYS_CLI_CODE_UNEXPECTED,
                ENOMEM, path);
            result = -1;
        }
    }
    for (size_t i = 0u; i < count; ++i) free(names[i]);
    free(names);
    return result;
}

int maelys_cli_extension_discover(
    const char *const *directories, size_t directory_count,
    maelys_cli_extension_set_t *out, maelys_cli_error_t *error) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    for (size_t i = 0u; i < directory_count; ++i) {
        if (!directories[i] || directories[i][0] != '/') {
            maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Use absolute command directories only.",
                "Command directory '%s' is not absolute.",
                directories[i] ? directories[i] : "");
            maelys_cli_extension_set_clear(out);
            return -1;
        }
        if (discover_directory(directories[i], out, error) != 0) {
            maelys_cli_extension_set_clear(out);
            return -1;
        }
    }
    return 0;
}

const maelys_cli_extension_t *maelys_cli_extension_find(
    const maelys_cli_extension_set_t *set, const char *command) {
    if (!set || !command) return NULL;
    for (size_t i = 0u; i < set->count; ++i)
        if (!strcmp(set->items[i].command, command)) return &set->items[i];
    return NULL;
}

void maelys_cli_extension_set_clear(maelys_cli_extension_set_t *set) {
    if (!set) return;
    free(set->items);
    set->items = NULL;
    set->count = 0u;
}
