
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ai.h"

static void escape_json_str(const char *str, char *out, size_t max) {
    char *p = out;
    size_t remaining = max - 1;
    
    while (*str && remaining > 6) {
        if (*str == '"' || *str == '\\') {
            *p++ = '\\';
            *p++ = *str++;
            remaining -= 2;
        } else if (*str == '\n') {
            *p++ = '\\';
            *p++ = 'n';
            str++;
            remaining -= 2;
        } else if (*str == '\r') {
            *p++ = '\\';
            *p++ = 'r';
            str++;
            remaining -= 2;
        } else if (*str == '\t') {
            *p++ = '\\';
            *p++ = 't';
            str++;
            remaining -= 2;
        } else if (iscntrl(*str)) {
            str++;
        } else {
            *p++ = *str++;
            remaining--;
        }
    }
    *p = '\0';
}

int chatgpt_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out) {
    char *key = get_api_key(cfg);
    if (!key) {
        fprintf(stderr, "No API key for chatgpt\n");
        return 1;
    }
    
    const char *model = cfg->model ? cfg->model : "gpt-4";
    
    char esc_input[32768];
    char esc_sys[32768];
    
    escape_json_str(input, esc_input, sizeof(esc_input));
    if (sys_prompt) {
        escape_json_str(sys_prompt, esc_sys, sizeof(esc_sys));
    }
    
    char body[65536];
    int n = 0;
    
    n += snprintf(body + n, sizeof(body) - n, "{\"model\":\"%s\",\"messages\":[", model);
    
    if (sys_prompt) {
        n += snprintf(body + n, sizeof(body) - n,
            "{\"role\":\"system\",\"content\":\"%s\"},", esc_sys);
    }
    
    n += snprintf(body + n, sizeof(body) - n,
        "{\"role\":\"user\",\"content\":\"%s\"}]}", esc_input);
    
    char headers[1024];
    snprintf(headers, sizeof(headers),
        "-H 'Content-Type: application/json' -H 'Authorization: Bearer %s'", key);
    
    return http_post("https://api.openai.com/v1/chat/completions", headers, body, out);
}
