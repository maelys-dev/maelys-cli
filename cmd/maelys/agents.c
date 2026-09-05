/*
 * `maelys agents install|status`: installs the agent instructions of the
 * CLI framework into a consumer project. AGENTS.md (Codex) and CLAUDE.md
 * (Claude Code) receive a managed block delimited by markers; the complete
 * guide and the Claude skill are generated files owned by the framework.
 */
#include "agents.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BEGIN_MARKER "<!-- maelys-cli:begin -->"
#define END_MARKER "<!-- maelys-cli:end -->"
#define MAX_HOST_FILE (4u * 1024u * 1024u)

enum client { CLIENT_ALL = 0, CLIENT_CLAUDE = 1, CLIENT_CODEX = 2 };
static const char *const client_choices[] = {"all", "claude", "codex", NULL};

const maelys_cli_operand_t maelys_agents_operands[1] = {
    {MAELYS_CLI_OPERAND("PROJECT_DIR",
     "Existing project directory that consumes libmaelys_cli.")},
};
const maelys_cli_option_t maelys_agents_install_options[2] = {
    {MAELYS_CLI_CHOICE("client",
     "Agent clients to configure: all, claude (CLAUDE.md and skill) or codex "
     "(AGENTS.md).", client_choices), .default_text = "all"},
    MAELYS_CLI_APPLY_OPTION,
};
const maelys_cli_option_t maelys_agents_status_options[1] = {
    {MAELYS_CLI_CHOICE("client", "Agent clients to inspect: all, claude or codex.",
     client_choices), .default_text = "all"},
};

const char maelys_agents_install_schema[] =
    "{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"mode\","
    "\"changed\",\"project\",\"client\",\"frameworkVersion\",\"files\"],"
    "\"properties\":{\"mode\":{\"enum\":[\"plan\",\"apply\"]},\"changed\":{"
    "\"type\":\"boolean\"},\"project\":{\"type\":\"string\"},\"client\":{"
    "\"enum\":[\"all\",\"claude\",\"codex\"]},\"frameworkVersion\":{\"type\":"
    "\"string\"},\"files\":{\"type\":\"array\",\"items\":{\"type\":\"object\","
    "\"additionalProperties\":false,\"required\":[\"path\",\"kind\",\"action\","
    "\"bytes\"],\"properties\":{\"path\":{\"type\":\"string\"},\"kind\":{\"enum\":"
    "[\"managed-block\",\"generated\"]},\"action\":{\"enum\":[\"create\","
    "\"update\",\"unchanged\"]},\"bytes\":{\"type\":\"integer\",\"minimum\":0}}}}}}";

const char maelys_agents_status_schema[] =
    "{\"type\":\"object\",\"additionalProperties\":false,\"required\":["
    "\"project\",\"client\",\"frameworkVersion\",\"upToDate\",\"files\"],"
    "\"properties\":{\"project\":{\"type\":\"string\"},\"client\":{\"enum\":"
    "[\"all\",\"claude\",\"codex\"]},\"frameworkVersion\":{\"type\":\"string\"},"
    "\"upToDate\":{\"type\":\"boolean\"},\"files\":{\"type\":\"array\",\"items\":"
    "{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"path\","
    "\"kind\",\"state\"],\"properties\":{\"path\":{\"type\":\"string\"},\"kind\":"
    "{\"enum\":[\"managed-block\",\"generated\"]},\"state\":{\"enum\":["
    "\"current\",\"missing\",\"outdated\",\"unmanaged\"]}}}}}}";

typedef struct managed_file {
    const char *relative;
    const char *text;   /* block body or whole generated content */
    int block;          /* 1: managed block inside a host file */
    int clients;        /* bit mask of CLIENT_CLAUDE | CLIENT_CODEX */
} managed_file_t;

static const managed_file_t *managed_files(size_t *out_count) {
    static managed_file_t files[4];
    files[0] = (managed_file_t){"AGENTS.md", maelys_agents_instructions_block,
        1, CLIENT_CODEX};
    files[1] = (managed_file_t){"CLAUDE.md", maelys_agents_instructions_block,
        1, CLIENT_CLAUDE};
    files[2] = (managed_file_t){"docs/maelys-cli-guide.md", maelys_agents_guide,
        0, CLIENT_CLAUDE | CLIENT_CODEX};
    files[3] = (managed_file_t){".claude/skills/maelys-cli-command/SKILL.md",
        maelys_agents_claude_skill, 0, CLIENT_CLAUDE};
    *out_count = 4u;
    return files;
}

static int client_mask(const maelys_cli_context_t *context) {
    size_t index = 0u;
    if (!maelys_cli_option_choice(context, "client", &index)) index = CLIENT_ALL;
    if (index == CLIENT_CLAUDE) return CLIENT_CLAUDE;
    if (index == CLIENT_CODEX) return CLIENT_CODEX;
    return CLIENT_CLAUDE | CLIENT_CODEX;
}

static const char *client_name(const maelys_cli_context_t *context) {
    size_t index = 0u;
    if (!maelys_cli_option_choice(context, "client", &index)) index = CLIENT_ALL;
    return client_choices[index];
}

static char *join(const char *first, const char *second, const char *third) {
    size_t total = strlen(first) + strlen(second) + (third ? strlen(third) : 0u);
    char *result = malloc(total + 1u);
    if (!result) return NULL;
    (void)snprintf(result, total + 1u, "%s%s%s", first, second, third ? third : "");
    return result;
}

static char *block_text(void) {
    const char *body = maelys_agents_instructions_block;
    size_t length = strlen(body);
    int needs_newline = length == 0u || body[length - 1u] != '\n';
    size_t total = sizeof(BEGIN_MARKER) + length + (needs_newline ? 1u : 0u) +
        sizeof(END_MARKER) + 1u;
    char *result = malloc(total);
    if (!result) return NULL;
    (void)snprintf(result, total, "%s\n%s%s%s\n", BEGIN_MARKER, body,
        needs_newline ? "\n" : "", END_MARKER);
    return result;
}

/* Computes the desired content of a host file. state receives the current
 * classification: 0 missing, 1 current, 2 outdated, 3 unmanaged. */
static int desired_block_content(
    const char *current, size_t current_size, char **out_desired,
    int *out_state) {
    char *block = block_text();
    if (!block) return -1;
    if (!current) {
        *out_desired = block;
        *out_state = 0;
        return 0;
    }
    const char *begin = strstr(current, BEGIN_MARKER);
    const char *end = begin ? strstr(begin, END_MARKER) : NULL;
    if (!begin || !end) {
        const char *separator = current_size == 0u ? "" :
            current[current_size - 1u] == '\n' ? "\n" : "\n\n";
        *out_desired = join(current, separator, block);
        free(block);
        *out_state = 3;
        return *out_desired ? 0 : -1;
    }
    const char *after = end + strlen(END_MARKER);
    if (*after == '\n') ++after;
    size_t prefix_length = (size_t)(begin - current);
    size_t suffix_length = strlen(after);
    size_t block_length = strlen(block);
    char *desired = malloc(prefix_length + block_length + suffix_length + 1u);
    if (!desired) {
        free(block);
        return -1;
    }
    memcpy(desired, current, prefix_length);
    memcpy(desired + prefix_length, block, block_length);
    memcpy(desired + prefix_length + block_length, after, suffix_length + 1u);
    free(block);
    *out_desired = desired;
    *out_state = strcmp(desired, current) == 0 ? 1 : 2;
    return 0;
}

/* Opens the parent of a repository-relative path one component at a time.
 * O_NOFOLLOW makes every existing parent a real directory, not a link. */
static int open_parent_at(
    int root, const char *relative, int create, int *out_parent,
    char *out_name, size_t out_name_size) {
    char copy[PATH_MAX];
    size_t length = relative ? strlen(relative) : 0u;
    if (root < 0 || length == 0u || length >= sizeof(copy) ||
        relative[0] == '/' || !out_parent || !out_name || out_name_size == 0u) {
        errno = EINVAL;
        return -1;
    }
    memcpy(copy, relative, length + 1u);
    int current = fcntl(root, F_DUPFD_CLOEXEC, 3);
    if (current < 0) return -1;
    char *component = copy;
    for (;;) {
        char *slash = strchr(component, '/');
        if (!slash) break;
        *slash = '\0';
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) {
            (void)close(current);
            errno = EINVAL;
            return -1;
        }
        int next = openat(current, component,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0 && errno == ENOENT && create) {
            if (mkdirat(current, component, 0755) != 0 && errno != EEXIST) {
                int saved = errno;
                (void)close(current);
                errno = saved;
                return -1;
            }
            next = openat(current, component,
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (next < 0) {
            int saved = errno;
            (void)close(current);
            errno = saved;
            return !create && saved == ENOENT ? 1 : -1;
        }
        (void)close(current);
        current = next;
        component = slash + 1;
    }
    if (!*component || !strcmp(component, ".") || !strcmp(component, "..") ||
        strlen(component) >= out_name_size) {
        (void)close(current);
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out_name, component, strlen(component) + 1u);
    *out_parent = current;
    return 0;
}

static int read_current(
    int project, const char *relative, char **out_text, size_t *out_size,
    int *out_exists) {
    unsigned char *bytes = NULL;
    size_t size = 0u;
    *out_text = NULL;
    *out_size = 0u;
    *out_exists = 0;
    int parent = -1;
    char name[PATH_MAX];
    int parent_result = open_parent_at(project, relative, 0, &parent, name,
        sizeof(name));
    if (parent_result == 1) return 0;
    if (parent_result != 0) return -1;
    int descriptor = openat(parent, name,
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int opened = errno;
    (void)close(parent);
    if (descriptor < 0) {
        if (opened == ENOENT) return 0;
        errno = opened;
        return -1;
    }
    struct stat status;
    int status_result = fstat(descriptor, &status);
    if (status_result != 0 || !S_ISREG(status.st_mode)) {
        int saved = status_result != 0 ? errno : EINVAL;
        (void)close(descriptor);
        errno = saved;
        return -1;
    }
    int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        int saved = errno;
        (void)close(descriptor);
        errno = saved;
        return -1;
    }
    int read_result = maelys_cli_read_descriptor(descriptor, MAX_HOST_FILE,
        &bytes, &size);
    int saved = errno;
    if (close(descriptor) != 0 && read_result == 0) {
        read_result = -1;
        saved = errno;
    }
    if (read_result != 0) {
        errno = saved;
        return -1;
    }
    if (size && memchr(bytes, '\0', size)) {
        free(bytes);
        errno = EILSEQ;
        return -1;
    }
    char *text = malloc(size + 1u);
    if (!text) {
        free(bytes);
        errno = ENOMEM;
        return -1;
    }
    if (size) memcpy(text, bytes, size);
    text[size] = '\0';
    free(bytes);
    *out_text = text;
    *out_size = size;
    *out_exists = 1;
    return 0;
}

static int resolve_project(
    maelys_cli_context_t *context, char *out_path, size_t out_size) {
    const char *operand = maelys_cli_operand(context, 0u);
    char resolved[PATH_MAX];
    struct stat status;
    if (!realpath(operand, resolved) || stat(resolved, &status) != 0 ||
        !S_ISDIR(status.st_mode)) {
        (void)maelys_cli_fail(context, MAELYS_CLI_CODE_NOT_FOUND,
            "Pass an existing project directory.",
            "Project directory %s does not exist or is not a directory.",
            operand);
        return -1;
    }
    if (strlen(resolved) >= out_size) {
        (void)maelys_cli_fail(context, MAELYS_CLI_CODE_VALIDATION_FAILED,
            "Use a shorter project path.", "Project path is too long.");
        return -1;
    }
    memcpy(out_path, resolved, strlen(resolved) + 1u);
    return 0;
}

static int write_file_atomic_at(
    int project, const char *relative, const void *bytes, size_t size,
    mode_t mode, maelys_cli_write_policy_t policy) {
    if ((!bytes && size) || !mode ||
        (policy != MAELYS_CLI_WRITE_REPLACE &&
         policy != MAELYS_CLI_WRITE_NO_REPLACE)) {
        errno = EINVAL;
        return -1;
    }
    int parent = -1;
    char name[PATH_MAX];
    if (open_parent_at(project, relative, 1, &parent, name, sizeof(name)) != 0)
        return -1;
    static unsigned long sequence;
    char temporary[96];
    int descriptor = -1;
    for (unsigned int attempt = 0u; attempt < 128u; ++attempt) {
        ++sequence;
        int written = snprintf(temporary, sizeof(temporary),
            ".maelys-cli.tmp.%ld.%lu", (long)getpid(), sequence);
        if (written < 0 || (size_t)written >= sizeof(temporary)) {
            errno = ENAMETOOLONG;
            break;
        }
        descriptor = openat(parent, temporary,
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor >= 0 || errno != EEXIST) break;
    }
    if (descriptor < 0) {
        int saved = errno;
        (void)close(parent);
        errno = saved;
        return -1;
    }
    int result = fchmod(descriptor, mode);
    size_t offset = 0u;
    while (result == 0 && offset < size) {
        ssize_t amount = write(descriptor,
            (const unsigned char *)bytes + offset, size - offset);
        if (amount > 0) offset += (size_t)amount;
        else if (amount < 0 && errno == EINTR) continue;
        else result = -1;
    }
    if (result == 0 && fsync(descriptor) != 0) result = -1;
    int saved = errno;
    if (close(descriptor) != 0 && result == 0) {
        result = -1;
        saved = errno;
    }
    if (result == 0) {
        if (policy == MAELYS_CLI_WRITE_REPLACE) {
            result = renameat(parent, temporary, parent, name);
        } else if (linkat(parent, temporary, parent, name, 0) != 0) {
            result = -1;
        } else {
            (void)unlinkat(parent, temporary, 0);
        }
        saved = errno;
    }
    if (result != 0) (void)unlinkat(parent, temporary, 0);
    if (close(parent) != 0 && result == 0) {
        result = -1;
        saved = errno;
    }
    errno = result == 0 ? 0 : (saved ? saved : EIO);
    return result;
}

typedef struct plan_entry {
    const managed_file_t *file;
    char path[PATH_MAX];
    char *desired;
    int state; /* 0 missing, 1 current, 2 outdated, 3 unmanaged */
} plan_entry_t;

static void free_plan(plan_entry_t *entries, size_t count) {
    for (size_t i = 0u; i < count; ++i) free(entries[i].desired);
}

static int build_plan(
    maelys_cli_context_t *context, int project_descriptor, const char *project,
    plan_entry_t *entries, size_t *out_count) {
    size_t file_count = 0u;
    const managed_file_t *files = managed_files(&file_count);
    int mask = client_mask(context);
    size_t count = 0u;
    memset(entries, 0, file_count * sizeof(*entries));
    for (size_t i = 0u; i < file_count; ++i) {
        if (!(files[i].clients & mask)) continue;
        plan_entry_t *entry = &entries[count];
        entry->file = &files[i];
        int written = snprintf(entry->path, sizeof(entry->path), "%s/%s",
            project, files[i].relative);
        if (written < 0 || (size_t)written >= sizeof(entry->path)) {
            free_plan(entries, count);
            (void)maelys_cli_fail(context, MAELYS_CLI_CODE_VALIDATION_FAILED,
                "Use a shorter project path.", "Target path is too long.");
            return -1;
        }
        char *current = NULL;
        size_t current_size = 0u;
        int exists = 0;
        if (read_current(project_descriptor, files[i].relative, &current,
                &current_size, &exists) != 0) {
            int saved = errno;
            free_plan(entries, count);
            (void)maelys_cli_fail_errno(context, MAELYS_CLI_CODE_IO_FAILED,
                saved, entry->path);
            return -1;
        }
        int result = 0;
        if (files[i].block) {
            result = desired_block_content(exists ? current : NULL,
                current_size, &entry->desired, &entry->state);
        } else {
            entry->desired = strdup(files[i].text);
            if (!entry->desired) result = -1;
            else entry->state = !exists || !current ? 0 :
                strcmp(current, entry->desired) == 0 ? 1 : 2;
        }
        free(current);
        if (result != 0) {
            free(entry->desired);
            entry->desired = NULL;
            free_plan(entries, count);
            (void)maelys_cli_fail_errno(context, MAELYS_CLI_CODE_UNEXPECTED,
                ENOMEM, entry->path);
            return -1;
        }
        ++count;
    }
    *out_count = count;
    return 0;
}

static const char *action_name(int state) {
    return state == 0 ? "create" : state == 1 ? "unchanged" : "update";
}

static const char *state_name(int state) {
    return state == 0 ? "missing" : state == 1 ? "current" :
        state == 2 ? "outdated" : "unmanaged";
}

int maelys_agents_install(maelys_cli_context_t *context) {
    char project[PATH_MAX];
    if (resolve_project(context, project, sizeof(project)) != 0)
        return MAELYS_CLI_EXIT_FAILURE;
    int project_descriptor = open(project,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (project_descriptor < 0)
        return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_IO_FAILED,
            errno, project);
    plan_entry_t entries[4];
    size_t count = 0u;
    if (build_plan(context, project_descriptor, project, entries, &count) != 0) {
        (void)close(project_descriptor);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    int apply = maelys_cli_flag(context, "apply");
    int changed = 0;
    for (size_t i = 0u; i < count; ++i) if (entries[i].state != 1) changed = 1;
    if (apply) {
        for (size_t i = 0u; i < count; ++i) {
            plan_entry_t *entry = &entries[i];
            if (entry->state == 1) continue;
            if (write_file_atomic_at(project_descriptor, entry->file->relative,
                    entry->desired,
                    strlen(entry->desired), 0644,
                    entry->state == 0 ? MAELYS_CLI_WRITE_NO_REPLACE :
                    MAELYS_CLI_WRITE_REPLACE) != 0) {
                int saved = errno;
                free_plan(entries, count);
                (void)close(project_descriptor);
                return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_IO_FAILED,
                    saved, entry->path);
            }
        }
    }
    (void)close(project_descriptor);
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    char human[4096];
    size_t used = 0u;
    int written = snprintf(human, sizeof(human), "%s agent instructions in %s "
        "(maelys-cli %s)\n", apply ? "Installed" : "Plan for", project,
        maelys_agents_version);
    if (written > 0 && (size_t)written < sizeof(human)) used = (size_t)written;
    int built = maelys_cli_json_begin_object(&writer) == 0 &&
        maelys_cli_json_key_string(&writer, "mode", apply ? "apply" : "plan") == 0 &&
        maelys_cli_json_key_boolean(&writer, "changed", apply && changed) == 0 &&
        maelys_cli_json_key_string(&writer, "project", project) == 0 &&
        maelys_cli_json_key_string(&writer, "client", client_name(context)) == 0 &&
        maelys_cli_json_key_string(&writer, "frameworkVersion",
            maelys_agents_version) == 0 &&
        maelys_cli_json_key(&writer, "files") == 0 &&
        maelys_cli_json_begin_array(&writer) == 0;
    for (size_t i = 0u; built && i < count; ++i) {
        const plan_entry_t *entry = &entries[i];
        built = maelys_cli_json_begin_object(&writer) == 0 &&
            maelys_cli_json_key_string(&writer, "path", entry->path) == 0 &&
            maelys_cli_json_key_string(&writer, "kind",
                entry->file->block ? "managed-block" : "generated") == 0 &&
            maelys_cli_json_key_string(&writer, "action",
                action_name(entry->state)) == 0 &&
            maelys_cli_json_key_unsigned(&writer, "bytes",
                (uint64_t)strlen(entry->desired)) == 0 &&
            maelys_cli_json_end_object(&writer) == 0;
        written = snprintf(human + used, sizeof(human) - used, "  %-10s %s\n",
            action_name(entry->state), entry->path);
        if (written > 0 && (size_t)written < sizeof(human) - used)
            used += (size_t)written;
    }
    built = built && maelys_cli_json_end_array(&writer) == 0 &&
        maelys_cli_json_end_object(&writer) == 0;
    free_plan(entries, count);
    if (!built) {
        maelys_cli_json_writer_clear(&writer);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not serialize the plan.");
    }
    if (!apply)
        (void)snprintf(human + used, sizeof(human) - used,
            "Plan only; add --apply to write these files.\n");
    return maelys_cli_succeed_writer(context, &writer, human, MAELYS_CLI_EXIT_OK);
}

int maelys_agents_status(maelys_cli_context_t *context) {
    char project[PATH_MAX];
    if (resolve_project(context, project, sizeof(project)) != 0)
        return MAELYS_CLI_EXIT_FAILURE;
    int project_descriptor = open(project,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (project_descriptor < 0)
        return maelys_cli_fail_errno(context, MAELYS_CLI_CODE_IO_FAILED,
            errno, project);
    plan_entry_t entries[4];
    size_t count = 0u;
    if (build_plan(context, project_descriptor, project, entries, &count) != 0) {
        (void)close(project_descriptor);
        return MAELYS_CLI_EXIT_FAILURE;
    }
    (void)close(project_descriptor);
    int up_to_date = 1;
    for (size_t i = 0u; i < count; ++i) if (entries[i].state != 1) up_to_date = 0;
    maelys_cli_json_writer_t writer;
    maelys_cli_json_writer_init(&writer);
    char human[4096];
    size_t used = 0u;
    int built = maelys_cli_json_begin_object(&writer) == 0 &&
        maelys_cli_json_key_string(&writer, "project", project) == 0 &&
        maelys_cli_json_key_string(&writer, "client", client_name(context)) == 0 &&
        maelys_cli_json_key_string(&writer, "frameworkVersion",
            maelys_agents_version) == 0 &&
        maelys_cli_json_key_boolean(&writer, "upToDate", up_to_date) == 0 &&
        maelys_cli_json_key(&writer, "files") == 0 &&
        maelys_cli_json_begin_array(&writer) == 0;
    for (size_t i = 0u; built && i < count; ++i) {
        const plan_entry_t *entry = &entries[i];
        built = maelys_cli_json_begin_object(&writer) == 0 &&
            maelys_cli_json_key_string(&writer, "path", entry->path) == 0 &&
            maelys_cli_json_key_string(&writer, "kind",
                entry->file->block ? "managed-block" : "generated") == 0 &&
            maelys_cli_json_key_string(&writer, "state",
                state_name(entry->state)) == 0 &&
            maelys_cli_json_end_object(&writer) == 0;
        int written = snprintf(human + used, sizeof(human) - used,
            "  %-10s %s\n", state_name(entry->state), entry->path);
        if (written > 0 && (size_t)written < sizeof(human) - used)
            used += (size_t)written;
    }
    built = built && maelys_cli_json_end_array(&writer) == 0 &&
        maelys_cli_json_end_object(&writer) == 0;
    free_plan(entries, count);
    if (!built) {
        maelys_cli_json_writer_clear(&writer);
        return maelys_cli_fail(context, MAELYS_CLI_CODE_UNEXPECTED, NULL,
            "Could not serialize the status.");
    }
    (void)snprintf(human + used, sizeof(human) - used, "%s\n", up_to_date ?
        "Agent instructions are current." :
        "Agent instructions are not current; run agents install --apply.");
    return maelys_cli_succeed_writer(context, &writer, human,
        up_to_date ? MAELYS_CLI_EXIT_OK : MAELYS_CLI_EXIT_VIOLATIONS);
}
