
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

int ollama_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out) {
    const char *model = cfg->model ? cfg->model : "llama2";
    
    char *esc_input = escape_json(input);
    char *esc_sys = sys_prompt ? escape_json(sys_prompt) : NULL;
    
    char body[65536];
    snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false%s%s%s}",
        model, esc_input,
        esc_sys ? ",\"system\":\"" : "",
        esc_sys ? esc_sys : "",
        esc_sys ? "\"" : "");
    
    char headers[256];
    snprintf(headers, sizeof(headers), "-H 'Content-Type: application/json'");
    
    int ret = http_post("http://localhost:11434/api/generate", headers, body, out);
    free(esc_input);
    if (esc_sys) free(esc_sys);
    
    return ret;
}
