#include "maelys/cli/extension.h"
#include "maelys/cli/digest.h"
#include "maelys/cli/files.h"
#include "maelys/cli/process.h"
#include "maelys/cli/version.h"
#include "internal.h"

#include <maelys/json.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    const maelys_json_document_t *document, maelys_json_value_t root,
    const char *key, int required, char *out, size_t out_size,
    const char *manifest, maelys_cli_error_t *error) {
    maelys_json_view_t view;
    maelys_json_result_t result = maelys_json_object_get_string(document, root,
        key, &view);
    if (result == MAELYS_JSON_ERR_NOT_FOUND) {
        if (!required) {
            out[0] = '\0';
            return 0;
        }
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Add the required manifest member.",
            "Manifest %s lacks '%s'.", manifest, key);
        return -1;
    }
    if (result != MAELYS_JSON_OK) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Use a JSON string for this member.",
            "Manifest %s member '%s' is not a string.", manifest, key);
        return -1;
    }
    if (view.size >= out_size || memchr(view.data, '\0', view.size)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Shorten the manifest member.",
            "Manifest %s member '%s' is too long or contains NUL.", manifest, key);
        return -1;
    }
    memcpy(out, view.data, view.size);
    out[view.size] = '\0';
    return 0;
}

/* A manifest that is sound but names a command this machine cannot run: the
 * command is declared and described unavailable rather than taking the whole
 * catalog down with it, and the code says which kind of cause it is -- an
 * agent must tell a version incompatibility from a refusal of trust without
 * reading a sentence. */
static void declare_unavailable(
    maelys_cli_extension_t *out, const char *code, const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
static void declare_unavailable(
    maelys_cli_extension_t *out, const char *code, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(out->unavailable, sizeof(out->unavailable), format,
        arguments);
    va_end(arguments);
    out->unavailable_code = code;
    out->digest_verified = 0;
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
    unsigned char *bytes = NULL;
    size_t size = 0u;
    /* A manifest is trusted exactly as the executable it declares: a
     * symbolic link is followed, and the file it resolves to must be a
     * regular file owned by root or the caller, closed to group and world,
     * in a directory only root or the caller may write. This loader refused
     * the link itself until MAELYS_CLI_FILE_TRUSTED_DIRECTORY existed, and
     * that refusal bought nothing a trusted directory does not buy better
     * — it is the directory, not the link, that says who may change what a
     * path resolves to — while making every package manager that links what
     * it installs from a cellar into its prefix, Homebrew above all,
     * undiscoverable. docs/extensions.md states the rule. */
    if (maelys_cli_read_trusted_file(manifest_path,
            MAELYS_CLI_FILE_REGULAR | MAELYS_CLI_FILE_TRUSTED_DIRECTORY |
            MAELYS_CLI_FILE_OWNER_TRUSTED |
            MAELYS_CLI_FILE_NOT_WRITABLE_BY_OTHERS, 2u,
            MAELYS_CLI_EXTENSION_MAX_MANIFEST_BYTES, &bytes, &size,
            &explanation) != 0) {
        int saved = errno;
        if (saved == EFBIG || saved == ENOMEM || saved == EIO) {
            maelys_cli_error_from_errno(error, MAELYS_CLI_CODE_IO_FAILED,
                saved, manifest_path);
        } else {
            /* The path an operator installed, and -- when it leads
             * elsewhere, which is what every package manager installs -- the
             * file that was actually judged, since that is where the fault
             * is and the named path looks irreproachable without it. */
            char resolved[PATH_MAX];
            char at[PATH_MAX + 16];
            struct stat entry_status;
            at[0] = '\0';
            if (lstat(manifest_path, &entry_status) == 0 &&
                S_ISLNK(entry_status.st_mode) &&
                realpath(manifest_path, resolved))
                (void)snprintf(at, sizeof(at), ", resolved to %s", resolved);
            maelys_cli_error_set(error, MAELYS_CLI_CODE_ACCESS_DENIED,
                "Install manifests as regular files owned by root or the "
                "current user, not writable by group or world, in a "
                "directory only root or that user may write.",
                "Manifest %s%s is untrusted: %s.", manifest_path, at,
                explanation ? explanation : strerror(saved));
        }
        return -1;
    }
    const maelys_json_limits_t limits = {
        MAELYS_CLI_EXTENSION_MAX_MANIFEST_BYTES, 8u, 1024u
    };
    maelys_json_document_t *document = NULL;
    maelys_json_error_t parse_error;
    maelys_json_result_t parsed = maelys_json_document_parse(bytes, size,
        MAELYS_JSON_PROFILE_RFC8259, &limits, &document, &parse_error);
    if (parsed != MAELYS_JSON_OK) {
        char detail[160];
        /* The RFC 6901 pointer of the failing value, which needs the bytes
         * the parser saw, hence computed before they are released. It
         * carries decoded manifest keys, and the metadata check below never
         * ran for a document that did not parse, so it is named only when it
         * is terminal-safe. Empty is the document root, and adds nothing. */
        char pointer[128] = "";
        char at[160] = "";
        (void)maelys_json_error_format(&parse_error, detail, sizeof(detail));
        (void)maelys_json_error_pointer(bytes, size, &parse_error, pointer,
            sizeof(pointer));
        free(bytes);
        if (pointer[0] && maelys_cli_text_is_terminal_safe(pointer))
            (void)snprintf(at, sizeof(at), " at %s", pointer);
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Repair or remove the manifest.",
            "Manifest %s is not valid JSON%s: %s.", manifest_path, at, detail);
        return -1;
    }
    free(bytes);
    int result = -1;
    maelys_json_value_t root = maelys_json_document_root(document);
    if (maelys_json_value_type(document, root) != MAELYS_JSON_TYPE_OBJECT) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Repair or remove the manifest.",
            "Manifest %s is not a JSON object.", manifest_path);
        goto done;
    }
    char schema[64];
    if (copy_string_field(document, root, "schema", 1, schema, sizeof(schema),
            manifest_path, error) != 0)
        goto done;
    if (strcmp(schema, MAELYS_CLI_EXTENSION_SCHEMA) != 0) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_UNSUPPORTED,
            "Reinstall the extension for this dispatcher version.",
            "Manifest %s declares unsupported schema %s.", manifest_path, schema);
        goto done;
    }
    if (copy_string_field(document, root, "command", 1, out->command,
            sizeof(out->command), manifest_path, error) != 0 ||
        copy_string_field(document, root, "executable", 1, out->executable,
            sizeof(out->executable), manifest_path, error) != 0 ||
        copy_string_field(document, root, "version", 1, out->version,
            sizeof(out->version), manifest_path, error) != 0 ||
        copy_string_field(document, root, "summary", 0, out->summary,
            sizeof(out->summary), manifest_path, error) != 0 ||
        copy_string_field(document, root, "sha256", 0, out->sha256,
            sizeof(out->sha256), manifest_path, error) != 0)
        goto done;
    if (!maelys_cli_text_is_terminal_safe(out->version) ||
        !maelys_cli_text_is_terminal_safe(out->summary)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Use one line of text without terminal control characters.",
            "Manifest %s has an unsafe 'version' or 'summary' member.",
            manifest_path);
        goto done;
    }
    if (!valid_command_name(out->command)) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use a lowercase command name that is not a built-in.",
            "Manifest %s declares invalid command '%s'.", manifest_path,
            out->command);
        goto done;
    }
    uint64_t api = 0u;
    if (maelys_json_object_get_u64(document, root, "cliApi", &api) != MAELYS_JSON_OK) {
        maelys_cli_error_set(error, MAELYS_CLI_CODE_PROTOCOL_FAILED,
            "Declare cliApi as an unsigned integer.",
            "Manifest %s lacks a valid 'cliApi'.", manifest_path);
        goto done;
    }
    out->cli_api = (unsigned int)api;
    if (api != MAELYS_CLI_API) {
        declare_unavailable(out, MAELYS_CLI_CODE_UNSUPPORTED,
            "manifest %s requires cliApi %llu; this dispatcher provides %d",
            manifest_path, (unsigned long long)api, MAELYS_CLI_API);
        result = 0;
        goto done;
    }
    char canonical_executable[PATH_MAX];
    const char *canonical_error = NULL;
    int canonical_missing = 0;
    if (out->executable[0] != '/') {
        canonical_error = "executable path must be absolute";
    } else if (!realpath(out->executable, canonical_executable)) {
        canonical_error = strerror(errno);
        canonical_missing = errno == ENOENT || errno == ENOTDIR;
    } else if (strlen(canonical_executable) >= sizeof(out->executable)) {
        canonical_error = "canonical executable path is too long";
    }
    if (out->executable[0] != '/') {
        /* A relative executable is a manifest that is wrong, not a machine
         * that cannot run it: no state of this machine makes it work, so it
         * is refused like any malformed declaration. */
        maelys_cli_error_set(error, MAELYS_CLI_CODE_ACCESS_DENIED,
            "Install the executable as an absolute, regular, trusted binary.",
            "Executable %s of manifest %s is unusable: %s.", out->executable,
            manifest_path, canonical_error);
        goto done;
    }
    if (canonical_error) {
        /* Gone is not the same as refused: an agent retries an install on
         * NOT_FOUND and never on ACCESS_DENIED. */
        declare_unavailable(out,
            canonical_missing ? MAELYS_CLI_CODE_NOT_FOUND :
                MAELYS_CLI_CODE_ACCESS_DENIED,
            "executable %s of manifest %s is unusable: %s", out->executable,
            manifest_path, canonical_error);
        result = 0;
        goto done;
    }
    if (maelys_cli_process_check_executable(canonical_executable,
            &explanation) != 0) {
        declare_unavailable(out,
            errno == ENOENT ? MAELYS_CLI_CODE_NOT_FOUND :
                MAELYS_CLI_CODE_ACCESS_DENIED,
            "executable %s of manifest %s is unusable: %s", out->executable,
            manifest_path, explanation ? explanation : strerror(errno));
        result = 0;
        goto done;
    }
    memcpy(out->executable, canonical_executable,
        strlen(canonical_executable) + 1u);
    if (out->sha256[0]) {
        char actual[MAELYS_CLI_SHA256_HEX_SIZE];
        if (strlen(out->sha256) != 64u ||
            maelys_cli_sha256_file(out->executable,
                MAELYS_CLI_EXTENSION_MAX_EXECUTABLE_BYTES, actual) != 0 ||
            strcmp(actual, out->sha256) != 0) {
            declare_unavailable(out, MAELYS_CLI_CODE_ACCESS_DENIED,
                "executable %s does not match the sha256 declared in %s",
                out->executable, manifest_path);
            result = 0;
            goto done;
        }
        out->digest_verified = 1;
    }
    result = 0;
done:
    maelys_json_document_release(document);
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
