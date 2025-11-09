#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "ai_core/agent.h"
#include "ai_core/core.h"

static const char* defaultKeyEnvForAi(const char *aiType) {
    if (!aiType) {
        return NULL;
    }

    if (strcmp(aiType, "chatgpt") == 0) {
        return "OPENAI_API_KEY";
    }
    if (strcmp(aiType, "claude") == 0) {
        return "ANTHROPIC_API_KEY";
    }
    if (strcmp(aiType, "deepseek") == 0) {
        return "DEEPSEEK_API_KEY";
    }

    return NULL;
}

char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

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
    if (read != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

char* read_stdin(void) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *resized = realloc(buf, cap);
            if (!resized) {
                free(buf);
                return NULL;
            }
            buf = resized;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

char* get_api_key(AIConfig *cfg) {
    if (cfg->key_raw) return cfg->key_raw;

    if (cfg->key_env) {
        char *value = getenv(cfg->key_env);
        if (value) {
            return value;
        }
    }

    const char *defaultEnv = defaultKeyEnvForAi(cfg->ai_type);
    if (defaultEnv) {
        return getenv(defaultEnv);
    }

    return NULL;
}

int http_post(const char *url, const char *headers, const char *body, FILE *out) {
    char tmppath[256];
    snprintf(tmppath, sizeof(tmppath), "/tmp/gipwrap_XXXXXX");
    int fd = mkstemp(tmppath);
    if (fd == -1) return -1;

    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlink(tmppath);
        return -1;
    }

    fputs(body, f);
    fclose(f);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "curl -s -X POST '%s' %s -d @%s", url, headers, tmppath);

    FILE *p = popen(cmd, "r");
    if (!p) {
        unlink(tmppath);
        return -1;
    }

    int c;
    while ((c = fgetc(p)) != EOF) {
        fputc(c, out);
    }

    pclose(p);
    unlink(tmppath);
    return 0;
}

char* find_json_string(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *key_pos = strstr(json, pattern);
    if (!key_pos) return NULL;

    const char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;

    const char *start = colon + 1;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }

    if (*start != '"') return NULL;
    start++;

    const char *end = start;
    while (*end) {
        if (*end == '\\' && *(end + 1)) {
            end += 2;
            continue;
        }
        if (*end == '"') break;
        end++;
    }

    size_t len = end - start;
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, start, len);
    result[len] = '\0';

    char *unescaped = malloc(len + 1);
    if (!unescaped) {
        free(result);
        return NULL;
    }
    char *dst = unescaped;
    const char *src = result;

    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;
            switch (*src) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case '"': *dst++ = '"'; break;
                case '\\': *dst++ = '\\'; break;
                default: *dst++ = *src; break;
            }
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    free(result);
    return unescaped;
}

char* extract_response(const char *ai_type, const char *json) {
    if (strcmp(ai_type, "ollama") == 0) {
        return find_json_string(json, "response");
    } else if (strcmp(ai_type, "chatgpt") == 0 || strcmp(ai_type, "deepseek") == 0) {
        const char *msg_start = strstr(json, "\"message\"");
        if (!msg_start) return NULL;
        return find_json_string(msg_start, "content");
    } else if (strcmp(ai_type, "claude") == 0) {
        return find_json_string(json, "text");
    }
    return NULL;
}

int callAiOnce(AIConfig *cfg, AIHandler handler, const char *input, const char *sys_prompt, char **raw_json_out, char **response_out) {
    FILE *tmp = tmpfile();
    if (!tmp) {
        return 1;
    }

    int ret = handler(cfg, input, sys_prompt, tmp);
    if (ret != 0) {
        fclose(tmp);
        return ret;
    }

    rewind(tmp);

    size_t cap = 8192;
    size_t len = 0;
    char *json = malloc(cap);
    if (!json) {
        fclose(tmp);
        return 1;
    }

    int c;
    while ((c = fgetc(tmp)) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *resized = realloc(json, cap);
            if (!resized) {
                free(json);
                fclose(tmp);
                return 1;
            }
            json = resized;
        }
        json[len++] = (char)c;
    }
    json[len] = '\0';

    fclose(tmp);

    char *response = extract_response(cfg->ai_type, json);

    if (response_out) {
        *response_out = response;
    } else if (response) {
        free(response);
    }

    if (raw_json_out) {
        *raw_json_out = json;
    } else {
        free(json);
    }

    return 0;
}

static int runStandardMode(AIConfig *cfg, AIHandler handler, const char *input, const char *sys_prompt, FILE *outf) {
    char *raw_json = NULL;
    char *response = NULL;
    int ret = callAiOnce(cfg, handler, input, sys_prompt, &raw_json, &response);
    if (ret != 0) {
        if (raw_json) free(raw_json);
        if (response) free(response);
        return ret;
    }

    if (cfg->verbose) {
        if (raw_json) {
            fprintf(outf, "%s", raw_json);
        }
    } else if (response) {
        fprintf(outf, "%s\n", response);
    } else if (raw_json) {
        fprintf(outf, "%s", raw_json);
    }

    if (raw_json) free(raw_json);
    if (response) free(response);
    return 0;
}

int ai_execute(AIConfig *cfg) {
    AIHandler handler = NULL;

    if (strcmp(cfg->ai_type, "chatgpt") == 0) {
        handler = chatgpt_call;
    } else if (strcmp(cfg->ai_type, "ollama") == 0) {
        handler = ollama_call;
    } else if (strcmp(cfg->ai_type, "claude") == 0) {
        handler = claude_call;
    } else if (strcmp(cfg->ai_type, "deepseek") == 0) {
        handler = deepseek_call;
    } else {
        fprintf(stderr, "Unknown AI: %s\n", cfg->ai_type);
        fprintf(stderr, "Available AIs: chatgpt, ollama, claude, deepseek\n");
        return 1;
    }

    char *input = cfg->input_file ? read_file(cfg->input_file) : read_stdin();
    if (!input) {
        fprintf(stderr, "Failed to read input\n");
        return 1;
    }

    char *sys_prompt = NULL;
    int sys_prompt_owned = 0;
    if (cfg->sys_prompt_file) {
        sys_prompt = read_file(cfg->sys_prompt_file);
        sys_prompt_owned = 1;
    } else if (cfg->sys_prompt) {
        sys_prompt = cfg->sys_prompt;
    }

    FILE *outf = cfg->output_file ? fopen(cfg->output_file, "w") : stdout;
    if (!outf) {
        free(input);
        if (sys_prompt_owned && sys_prompt) free(sys_prompt);
        return 1;
    }

    int ret;
    if (cfg->agentMode) {
        ret = runAgentMode(cfg, handler, input, sys_prompt, outf);
    } else {
        ret = runStandardMode(cfg, handler, input, sys_prompt, outf);
    }

    if (cfg->output_file) fclose(outf);
    free(input);
    if (sys_prompt_owned && sys_prompt) free(sys_prompt);

    return ret;
}
