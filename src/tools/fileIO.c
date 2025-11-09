#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "tools.h"

static char* duplicateString(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, src, len + 1);
    return out;
}

static char* formatString(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        return NULL;
    }

    char *buffer = malloc((size_t)needed + 1);
    if (!buffer) return NULL;

    va_start(args, fmt);
    vsnprintf(buffer, (size_t)needed + 1, fmt, args);
    va_end(args);

    return buffer;
}

static char* agentToolReadFile(const char *argument, char **error_out) {
    if (!argument || !*argument) {
        if (error_out) *error_out = duplicateString("readFile requires a file path argument.");
        return NULL;
    }

    char *contents = read_file(argument);
    if (!contents) {
        if (error_out) *error_out = formatString("Failed to read file '%s'.", argument);
        return NULL;
    }

    return contents;
}

static char* agentToolListDir(const char *argument, char **error_out) {
    const char *path = (argument && *argument) ? argument : ".";
    DIR *dir = opendir(path);
    if (!dir) {
        if (error_out) *error_out = formatString("Failed to open directory '%s': %s", path, strerror(errno));
        return NULL;
    }

    size_t cap = 256;
    size_t len = 0;
    char *buffer = malloc(cap);
    if (!buffer) {
        closedir(dir);
        if (error_out) *error_out = duplicateString("Out of memory while listing directory.");
        return NULL;
    }
    buffer[0] = '\0';

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        size_t name_len = strlen(entry->d_name);
        while (len + name_len + 2 >= cap) {
            cap *= 2;
            char *resized = realloc(buffer, cap);
            if (!resized) {
                free(buffer);
                closedir(dir);
                if (error_out) *error_out = duplicateString("Out of memory while listing directory.");
                return NULL;
            }
            buffer = resized;
        }
        memcpy(buffer + len, entry->d_name, name_len);
        len += name_len;
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }
    closedir(dir);

    if (len == 0) {
        const char *empty_msg = "(empty directory)\n";
        size_t msg_len = strlen(empty_msg);
        char *resized = realloc(buffer, msg_len + 1);
        if (!resized) {
            free(buffer);
            if (error_out) *error_out = duplicateString("Out of memory while finalizing directory listing.");
            return NULL;
        }
        buffer = resized;
        memcpy(buffer, empty_msg, msg_len + 1);
    }

    return buffer;
}

static const AgentTool fileTools[] = {
    { "readFile", "Read the contents of a UTF-8 text file.", agentToolReadFile },
    { "listDir", "List files within a directory as newline separated entries.", agentToolListDir }
};

const AgentTool* getAgentTools(size_t *count) {
    if (count) {
        *count = sizeof(fileTools) / sizeof(fileTools[0]);
    }
    return fileTools;
}
