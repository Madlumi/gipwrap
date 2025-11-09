#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

static void trimWhitespaceInPlace(char *text) {
    if (!text) {
        return;
    }

    char *start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    size_t newLen = (size_t)(end - start);
    if (start != text && newLen > 0) {
        memmove(text, start, newLen);
    } else if (start != text) {
        memmove(text, start, 0);
    }
    text[newLen] = '\0';
}

static char* duplicateTrimmed(const char *text) {
    if (!text) {
        return NULL;
    }
    char *dup = duplicateString(text);
    if (!dup) {
        return NULL;
    }
    trimWhitespaceInPlace(dup);
    return dup;
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

static int ensureDirectoryExists(const char *path) {
    if (!path || !*path) {
        return -1;
    }

    if (mkdir(path, 0700) == 0) {
        return 0;
    }

    if (errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return 0;
        }
    }

    return -1;
}

static int ensureParentDirectories(const char *path) {
    if (!path) {
        return -1;
    }

    char *copy = duplicateString(path);
    if (!copy) {
        return -1;
    }

    char *slash = strrchr(copy, '/');
    if (!slash) {
        free(copy);
        return 0;
    }

    *slash = '\0';
    if (*copy == '\0') {
        free(copy);
        return 0;
    }

    for (char *p = copy + 1; *p; ++p) {
        if (*p == '/') {
            char saved = *p;
            *p = '\0';
            if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
                free(copy);
                return -1;
            }
            *p = saved;
        }
    }

    if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
        free(copy);
        return -1;
    }

    free(copy);
    return 0;
}

static char* ensureAiDirPath(char **error_out) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        if (error_out) *error_out = duplicateString("HOME environment variable is not set.");
        return NULL;
    }

    char *aiDir = formatString("%s/.gipwrap", home);
    if (!aiDir) {
        if (error_out) *error_out = duplicateString("Failed to allocate memory for aiDir path.");
        return NULL;
    }

    if (ensureDirectoryExists(aiDir) != 0) {
        if (error_out) *error_out = formatString("Failed to create AI directory at %s: %s", aiDir, strerror(errno));
        free(aiDir);
        return NULL;
    }

    return aiDir;
}

static int isSafeRelativePath(const char *relative) {
    if (!relative || !*relative) {
        return 0;
    }

    if (relative[0] == '/' || relative[0] == '~') {
        return 0;
    }

    const char *ptr = relative;
    while (*ptr) {
        if (*ptr == '.') {
            int atSegmentStart = (ptr == relative) || (*(ptr - 1) == '/');
            if (atSegmentStart) {
                if (*(ptr + 1) == '.') {
                    char next = *(ptr + 2);
                    if (next == '/' || next == '\0') {
                        return 0;
                    }
                }
            }
        }
        ptr++;
    }

    return 1;
}

static char* joinAiDir(const char *relative, char **error_out) {
    if (!isSafeRelativePath(relative)) {
        if (error_out) *error_out = formatString("Path '%s' must be relative to ~/.gipwrap without '..'.", relative ? relative : "");
        return NULL;
    }

    char *aiDir = ensureAiDirPath(error_out);
    if (!aiDir) {
        return NULL;
    }

    char *fullPath = formatString("%s/%s", aiDir, relative);
    free(aiDir);
    if (!fullPath && error_out) {
        *error_out = duplicateString("Failed to build path inside aiDir.");
    }
    return fullPath;
}

static char* escapeJsonString(const char *src) {
    if (!src) {
        return duplicateString("");
    }

    size_t extra = 0;
    for (const char *p = src; *p; ++p) {
        switch (*p) {
            case '\\':
            case '"':
            case '\n':
            case '\r':
            case '\t':
                extra++;
                break;
            default:
                if ((unsigned char)*p < 0x20) {
                    extra += 5;
                }
                break;
        }
    }

    size_t len = strlen(src);
    char *out = malloc(len + extra + 1);
    if (!out) {
        return NULL;
    }

    char *dst = out;
    for (const char *p = src; *p; ++p) {
        switch (*p) {
            case '\\': *dst++ = '\\'; *dst++ = '\\'; break;
            case '"':  *dst++ = '\\'; *dst++ = '"'; break;
            case '\n': *dst++ = '\\'; *dst++ = 'n'; break;
            case '\r': *dst++ = '\\'; *dst++ = 'r'; break;
            case '\t': *dst++ = '\\'; *dst++ = 't'; break;
            default:
                if ((unsigned char)*p < 0x20) {
                    sprintf(dst, "\\u%04x", (unsigned char)*p);
                    dst += 6;
                } else {
                    *dst++ = *p;
                }
                break;
        }
    }
    *dst = '\0';
    return out;
}

static char* readFileIfExists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (read != (size_t)len) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static char* createTimestampedName(const char *prefix, const char *extension) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    static int counter = 0;
    counter = (counter + 1) % 1000;

    char buffer[128];
    if (strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm_now) == 0) {
        snprintf(buffer, sizeof(buffer), "%ld", (long)now);
    }

    return formatString("%s%s_%03d.%s", prefix ? prefix : "", buffer, counter, extension ? extension : "dat");
}

static char* runCommandInAiDir(const char *command, int *exitCode, char **error_out) {
    if (!command || !*command) {
        if (error_out) *error_out = duplicateString("No command provided to run.");
        return NULL;
    }

    char *aiDirError = NULL;
    char *aiDir = ensureAiDirPath(&aiDirError);
    if (!aiDir) {
        if (error_out) *error_out = aiDirError ? aiDirError : duplicateString("Failed to resolve AI directory.");
        return NULL;
    }

    char *shellCmd = formatString("cd '%s' && AIDIR='%s' %s", aiDir, aiDir, command);
    free(aiDir);
    if (!shellCmd) {
        if (error_out) *error_out = duplicateString("Failed to allocate command buffer.");
        return NULL;
    }

    FILE *pipe = popen(shellCmd, "r");
    free(shellCmd);
    if (!pipe) {
        if (error_out) *error_out = duplicateString("Failed to execute command with popen.");
        return NULL;
    }

    size_t cap = 256;
    size_t len = 0;
    char *output = malloc(cap);
    if (!output) {
        pclose(pipe);
        if (error_out) *error_out = duplicateString("Out of memory capturing command output.");
        return NULL;
    }

    int ch;
    while ((ch = fgetc(pipe)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *resized = realloc(output, cap);
            if (!resized) {
                free(output);
                pclose(pipe);
                if (error_out) *error_out = duplicateString("Out of memory capturing command output.");
                return NULL;
            }
            output = resized;
        }
        output[len++] = (char)ch;
    }
    output[len] = '\0';

    int status = pclose(pipe);
    int code = -1;
    if (status == -1) {
        if (error_out) *error_out = duplicateString("Failed to retrieve command status.");
        free(output);
        return NULL;
    } else if (WIFEXITED(status)) {
        code = WEXITSTATUS(status);
    } else {
        code = -1;
    }

    if (exitCode) {
        *exitCode = code;
    }

    if (code != 0) {
        if (error_out) *error_out = formatString("Command exited with code %d. Output:%s%s", code, len ? "\n" : " ", len ? output : "(no output)");
        free(output);
        return NULL;
    }

    if (len == 0) {
        free(output);
        return duplicateString("Command executed successfully with no output.");
    }

    return output;
}

static int parseOutputAndBody(const char *argument, char **outputName, char **body, char **error_out) {
    if (!argument) {
        if (error_out) *error_out = duplicateString("Tool input was empty.");
        return -1;
    }

    const char *ptr = argument;
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    const char *newline = strchr(ptr, '\n');
    size_t firstLen = newline ? (size_t)(newline - ptr) : strlen(ptr);
    char *firstLine = malloc(firstLen + 1);
    if (!firstLine) {
        if (error_out) *error_out = duplicateString("Failed to allocate buffer for parsing tool input.");
        return -1;
    }
    memcpy(firstLine, ptr, firstLen);
    firstLine[firstLen] = '\0';
    trimWhitespaceInPlace(firstLine);

    char *name = NULL;
    const char *bodyStart = ptr;

    if (strncasecmp(firstLine, "output=", 7) == 0) {
        name = duplicateTrimmed(firstLine + 7);
        bodyStart = newline ? newline + 1 : ptr + firstLen;
    } else if (strncasecmp(firstLine, "file=", 5) == 0) {
        name = duplicateTrimmed(firstLine + 5);
        bodyStart = newline ? newline + 1 : ptr + firstLen;
    } else if (strncasecmp(firstLine, "path=", 5) == 0) {
        name = duplicateTrimmed(firstLine + 5);
        bodyStart = newline ? newline + 1 : ptr + firstLen;
    } else {
        char *colon = strchr(firstLine, ':');
        if (colon) {
            *colon = '\0';
            if (strcasecmp(firstLine, "output") == 0 || strcasecmp(firstLine, "file") == 0 || strcasecmp(firstLine, "path") == 0) {
                name = duplicateTrimmed(colon + 1);
                bodyStart = newline ? newline + 1 : ptr + firstLen;
            }
        }
    }

    free(firstLine);

    char *bodyText = duplicateTrimmed(bodyStart);
    if (!bodyText) {
        if (name) free(name);
        if (error_out) *error_out = duplicateString("Failed to allocate body buffer.");
        return -1;
    }

    if (!*bodyText) {
        free(bodyText);
        if (name) free(name);
        if (error_out) *error_out = duplicateString("Tool input body is empty.");
        return -1;
    }

    if (outputName) {
        *outputName = name;
    } else if (name) {
        free(name);
    }

    if (body) {
        *body = bodyText;
    } else {
        free(bodyText);
    }

    return 0;
}

static char* agentToolSaveMemory(const char *argument, char **error_out) {
    char *memoryText = duplicateTrimmed(argument);
    if (!memoryText || !*memoryText) {
        if (memoryText) free(memoryText);
        if (error_out) *error_out = duplicateString("Provide text to save in memory.");
        return NULL;
    }

    char *escaped = escapeJsonString(memoryText);
    free(memoryText);
    if (!escaped) {
        if (error_out) *error_out = duplicateString("Failed to prepare JSON payload for memory entry.");
        return NULL;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[64];
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &tm_now) == 0) {
        snprintf(timestamp, sizeof(timestamp), "%ld", (long)now);
    }

    char *entry = formatString("  {\"timestamp\":\"%s\",\"memory\":\"%s\"}", timestamp, escaped);
    free(escaped);
    if (!entry) {
        if (error_out) *error_out = duplicateString("Failed to format memory entry.");
        return NULL;
    }

    char *aiDir = ensureAiDirPath(error_out);
    if (!aiDir) {
        free(entry);
        return NULL;
    }

    char *memoryPath = formatString("%s/memory.json", aiDir);
    free(aiDir);
    if (!memoryPath) {
        free(entry);
        if (error_out) *error_out = duplicateString("Failed to build memory file path.");
        return NULL;
    }

    char *existing = readFileIfExists(memoryPath);
    char *newFile = NULL;

    if (!existing) {
        newFile = formatString("[\n%s\n]\n", entry);
    } else {
        size_t len = strlen(existing);
        if (len < 2 || existing[0] != '[') {
            newFile = formatString("[\n%s\n]\n", entry);
        } else {
            char *body = NULL;
            if (len >= 2) {
                body = malloc(len - 1);
                if (body) {
                    memcpy(body, existing + 1, len - 2);
                    body[len - 2] = '\0';
                    trimWhitespaceInPlace(body);
                }
            }

            if (!body) {
                newFile = formatString("[\n%s\n]\n", entry);
            } else if (*body) {
                newFile = formatString("[\n%s,\n%s\n]\n", body, entry);
            } else {
                newFile = formatString("[\n%s\n]\n", entry);
            }

            if (body) free(body);
        }
        free(existing);
    }

    if (!newFile) {
        free(entry);
        free(memoryPath);
        if (error_out) *error_out = duplicateString("Failed to build updated memory document.");
        return NULL;
    }

    FILE *f = fopen(memoryPath, "w");
    if (!f) {
        char *msg = formatString("Failed to open %s for writing: %s", memoryPath, strerror(errno));
        free(entry);
        free(memoryPath);
        free(newFile);
        if (error_out) {
            *error_out = msg ? msg : duplicateString("Failed to open memory file for writing.");
        } else if (msg) {
            free(msg);
        }
        return NULL;
    }

    fputs(newFile, f);
    fclose(f);

    free(entry);
    free(memoryPath);
    free(newFile);

    return duplicateString("Memory saved to ~/.gipwrap/memory.json.");
}

static char* agentToolGetMemories(const char *argument, char **error_out) {
    (void)argument;

    char *aiDir = ensureAiDirPath(error_out);
    if (!aiDir) {
        return NULL;
    }

    char *memoryPath = formatString("%s/memory.json", aiDir);
    free(aiDir);
    if (!memoryPath) {
        if (error_out) *error_out = duplicateString("Failed to build memory path.");
        return NULL;
    }

    char *contents = readFileIfExists(memoryPath);
    free(memoryPath);

    if (!contents) {
        return duplicateString("No memories stored yet.");
    }

    return contents;
}

static int synthesizeSpeechFile(const char *text, const char *requestedName, char **relativeOut, char **commandOutput, char **error_out) {
    char *trimmedText = duplicateTrimmed(text);
    if (!trimmedText || !*trimmedText) {
        if (trimmedText) free(trimmedText);
        if (error_out) *error_out = duplicateString("Provide text to synthesize.");
        return -1;
    }

    char *relative = NULL;
    if (requestedName && *requestedName) {
        relative = duplicateTrimmed(requestedName);
    } else {
        relative = createTimestampedName("audio_", "wav");
    }

    if (!relative) {
        free(trimmedText);
        if (error_out) *error_out = duplicateString("Failed to prepare output filename.");
        return -1;
    }

    if (!isSafeRelativePath(relative)) {
        if (error_out) *error_out = formatString("Invalid audio path '%s'. Use a relative path inside ~/.gipwrap.", relative);
        free(trimmedText);
        free(relative);
        return -1;
    }

    char *absolute = joinAiDir(relative, error_out);
    if (!absolute) {
        free(trimmedText);
        free(relative);
        return -1;
    }

    if (ensureParentDirectories(absolute) != 0) {
        if (error_out) *error_out = formatString("Failed to prepare directories for %s: %s", absolute, strerror(errno));
        free(trimmedText);
        free(relative);
        free(absolute);
        return -1;
    }

    char tmpTemplate[] = "/tmp/gipwrap_tts_XXXXXX";
    int fd = mkstemp(tmpTemplate);
    if (fd == -1) {
        if (error_out) *error_out = duplicateString("Failed to create temporary file for TTS input.");
        free(trimmedText);
        free(relative);
        free(absolute);
        return -1;
    }

    FILE *tmpFile = fdopen(fd, "w");
    if (!tmpFile) {
        if (error_out) *error_out = duplicateString("Failed to open temporary file stream for TTS.");
        close(fd);
        unlink(tmpTemplate);
        free(trimmedText);
        free(relative);
        free(absolute);
        return -1;
    }

    fputs(trimmedText, tmpFile);
    fclose(tmpFile);
    free(trimmedText);

    char *command = formatString("text2wave -eval \"(voice_cmu_us_slt_arctic_hts)\" '%s' -o '%s'", tmpTemplate, absolute);
    if (!command) {
        if (error_out) *error_out = duplicateString("Failed to construct text-to-speech command.");
        unlink(tmpTemplate);
        free(relative);
        free(absolute);
        return -1;
    }

    int status = 0;
    char *cmdOutput = runCommandInAiDir(command, &status, error_out);
    free(command);
    unlink(tmpTemplate);

    if (!cmdOutput) {
        free(relative);
        free(absolute);
        return -1;
    }

    if (access(absolute, F_OK) != 0) {
        if (error_out) *error_out = formatString("Audio file was not created at %s.", absolute);
        free(cmdOutput);
        free(relative);
        free(absolute);
        return -1;
    }

    if (commandOutput) {
        *commandOutput = cmdOutput;
    } else {
        free(cmdOutput);
    }

    if (relativeOut) {
        *relativeOut = relative;
    } else {
        free(relative);
    }

    free(absolute);
    return 0;
}

static char* agentToolGenerateAudio(const char *argument, char **error_out) {
    char *outputName = NULL;
    char *text = NULL;
    if (parseOutputAndBody(argument, &outputName, &text, error_out) != 0) {
        return NULL;
    }

    char *relative = NULL;
    char *cmdOutput = NULL;
    if (synthesizeSpeechFile(text, outputName, &relative, &cmdOutput, error_out) != 0) {
        if (outputName) free(outputName);
        free(text);
        return NULL;
    }

    free(text);
    if (outputName) free(outputName);

    char *message = formatString("Audio saved to %s.\n%s", relative, cmdOutput ? cmdOutput : "");
    free(cmdOutput);
    if (!message) {
        free(relative);
        if (error_out) *error_out = duplicateString("Failed to format audio generation message.");
        return NULL;
    }

    free(relative);
    return message;
}

static char* agentToolPlayAudio(const char *argument, char **error_out) {
    char *relative = duplicateTrimmed(argument);
    if (!relative || !*relative) {
        if (relative) free(relative);
        if (error_out) *error_out = duplicateString("Provide a relative path to the audio file or directory inside ~/.gipwrap.");
        return NULL;
    }

    if (!isSafeRelativePath(relative)) {
        if (error_out) *error_out = formatString("Invalid audio path '%s'.", relative);
        free(relative);
        return NULL;
    }

    char *absolute = joinAiDir(relative, error_out);
    if (!absolute) {
        free(relative);
        return NULL;
    }

    struct stat st;
    if (stat(absolute, &st) != 0) {
        if (error_out) *error_out = formatString("Path %s does not exist.", absolute);
        free(relative);
        free(absolute);
        return NULL;
    }

    char *command = formatString("mpv --no-video --loop-playlist=0 --speed=1.0 '%s'", relative);
    free(absolute);
    if (!command) {
        if (error_out) *error_out = duplicateString("Failed to build mpv command.");
        free(relative);
        return NULL;
    }

    int status = 0;
    char *output = runCommandInAiDir(command, &status, error_out);
    free(command);
    if (!output) {
        free(relative);
        return NULL;
    }

    char *message = formatString("Playback command executed for %s.\n%s", relative, output);
    free(output);
    free(relative);
    if (!message && error_out) {
        *error_out = duplicateString("Failed to build playback result message.");
    }
    return message;
}

static char* agentToolGenerateImage(const char *argument, char **error_out) {
    char *outputName = NULL;
    char *body = NULL;
    if (parseOutputAndBody(argument, &outputName, &body, error_out) != 0) {
        return NULL;
    }

    if (!outputName) {
        outputName = createTimestampedName("image_", "png");
        if (!outputName) {
            free(body);
            if (error_out) *error_out = duplicateString("Failed to create a default image filename.");
            return NULL;
        }
    }

    if (!isSafeRelativePath(outputName)) {
        if (error_out) *error_out = formatString("Invalid image output path '%s'.", outputName);
        free(outputName);
        free(body);
        return NULL;
    }

    char *absolute = joinAiDir(outputName, error_out);
    if (!absolute) {
        free(outputName);
        free(body);
        return NULL;
    }

    if (ensureParentDirectories(absolute) != 0) {
        if (error_out) *error_out = formatString("Failed to prepare directories for %s: %s", absolute, strerror(errno));
        free(outputName);
        free(body);
        free(absolute);
        return NULL;
    }

    char *command = NULL;
    if (strncasecmp(body, "convert", 7) == 0 || strncasecmp(body, "magick", 6) == 0) {
        command = duplicateString(body);
    } else {
        command = formatString("convert %s '%s'", body, outputName);
    }

    free(body);
    if (!command) {
        if (error_out) *error_out = duplicateString("Failed to build ImageMagick command.");
        free(outputName);
        free(absolute);
        return NULL;
    }

    int status = 0;
    char *cmdOutput = runCommandInAiDir(command, &status, error_out);
    free(command);
    if (!cmdOutput) {
        free(outputName);
        free(absolute);
        return NULL;
    }

    if (access(absolute, F_OK) != 0) {
        if (error_out) *error_out = formatString("Image was not created at %s.", absolute);
        free(cmdOutput);
        free(outputName);
        free(absolute);
        return NULL;
    }

    char *message = formatString("Image saved to %s.\n%s", outputName, cmdOutput);
    free(cmdOutput);
    free(absolute);
    if (!message) {
        if (error_out) *error_out = duplicateString("Failed to build image generation message.");
        free(outputName);
        return NULL;
    }

    free(outputName);
    return message;
}

static char* agentToolPlayTts(const char *argument, char **error_out) {
    char *outputName = NULL;
    char *text = NULL;
    if (parseOutputAndBody(argument, &outputName, &text, error_out) != 0) {
        return NULL;
    }

    char *relative = NULL;
    if (synthesizeSpeechFile(text, outputName, &relative, NULL, error_out) != 0) {
        if (outputName) free(outputName);
        free(text);
        return NULL;
    }

    free(text);
    if (outputName) free(outputName);

    char *command = formatString("mpv --no-video --speed=2.0 '%s'", relative);
    if (!command) {
        if (error_out) *error_out = duplicateString("Failed to construct mpv command for TTS playback.");
        free(relative);
        return NULL;
    }

    int status = 0;
    char *output = runCommandInAiDir(command, &status, error_out);
    free(command);
    if (!output) {
        free(relative);
        return NULL;
    }

    char *message = formatString("Generated and played audio at 2x speed from %s.\n%s", relative, output);
    free(output);
    free(relative);
    if (!message && error_out) {
        *error_out = duplicateString("Failed to build TTS playback message.");
    }
    return message;
}

static const AgentTool fileTools[] = {
    { "readFile", "Read the contents of a UTF-8 text file.", agentToolReadFile },
    { "listDir", "List files within a directory as newline separated entries.", agentToolListDir },
    { "saveMemory", "Append a timestamped memory entry to ~/.gipwrap/memory.json.", agentToolSaveMemory },
    { "getMemories", "Retrieve all stored memory entries from ~/.gipwrap/memory.json.", agentToolGetMemories },
    { "generateImage", "Use ImageMagick. Optional first line: output=<relative path>. Body: convert arguments or full command.", agentToolGenerateImage },
    { "generateAudio", "Create speech audio with festival. Optional first line output=<relative path>. Body: text to speak.", agentToolGenerateAudio },
    { "playAudio", "Play an audio file or directory inside ~/.gipwrap using mpv.", agentToolPlayAudio },
    { "playTts", "Generate speech and play it immediately at 2x speed. Optional first line output=<relative path>.", agentToolPlayTts }
};

const AgentTool* getAgentTools(size_t *count) {
    if (count) {
        *count = sizeof(fileTools) / sizeof(fileTools[0]);
    }
    return fileTools;
}
