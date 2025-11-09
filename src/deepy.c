
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"

static char* escape_json(const char *str) {
    size_t len = strlen(str);
    char *esc = malloc(len * 2 + 1);
    char *p = esc;
    
    while (*str) {
        if (*str == '"' || *str == '\\') *p++ = '\\';
        if (*str == '\n') {
            *p++ = '\\';
            *p++ = 'n';
            str++;
        } else {
            *p++ = *str++;
        }
    }
    *p = '\0';
    return esc;
}

int deepseek_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out) {
    char *key = get_api_key(cfg);
    if (!key) {
        fprintf(stderr, "No API key provided\n");
        return 1;
    }
    
    const char *model = cfg->model ? cfg->model : "deepseek-chat";
    
    char *esc_input = escape_json(input);
    char *esc_sys = sys_prompt ? escape_json(sys_prompt) : NULL;
    
    char body[65536];
    int pos = snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"messages\":[", model);
    
    if (esc_sys) {
        pos += snprintf(body + pos, sizeof(body) - pos,
            "{\"role\":\"system\",\"content\":\"%s\"},", esc_sys);
    }
    
    pos += snprintf(body + pos, sizeof(body) - pos,
        "{\"role\":\"user\",\"content\":\"%s\"}]}", esc_input);
    
    char headers[512];
    snprintf(headers, sizeof(headers),
        "-H 'Content-Type: application/json' -H 'Authorization: Bearer %s'", key);
    
    int ret = http_post("https://api.deepseek.com/chat/completions", headers, body, out);
    free(esc_input);
    if (esc_sys) free(esc_sys);
    
    return ret;
}
