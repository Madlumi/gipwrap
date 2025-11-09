#ifndef AI_CORE_CORE_H
#define AI_CORE_CORE_H

#include <stdio.h>
#include "ai.h"

char* find_json_string(const char *json, const char *key);
char* extract_response(const char *ai_type, const char *json);
int callAiOnce(AIConfig *cfg, AIHandler handler, const char *input, const char *sys_prompt, char **raw_json_out, char **response_out);

#endif
