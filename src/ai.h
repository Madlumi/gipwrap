
#ifndef AI_H
#define AI_H

#include <stdio.h>

typedef struct {
    char *ai_type;
    char *input_file;
    char *output_file;
    char *sys_prompt_file;
    char *sys_prompt;
    char *model;
    char *key_env;
    char *key_raw;
    int verbose;
    int agentMode;
    int agentThinking;
} AIConfig;

typedef int (*AIHandler)(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out);

int ai_execute(AIConfig *cfg);
char* read_file(const char *path);
char* read_stdin(void);
char* get_api_key(AIConfig *cfg);
int http_post(const char *url, const char *headers, const char *body, FILE *out);
char* extract_response(const char *ai_type, const char *json);

int chatgpt_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out);
int ollama_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out);
int claude_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out);
int deepseek_call(AIConfig *cfg, const char *input, const char *sys_prompt, FILE *out);

#endif
