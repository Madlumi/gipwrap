#ifndef AI_CORE_AGENT_H
#define AI_CORE_AGENT_H

#include <stdio.h>
#include "ai.h"

int runAgentMode(AIConfig *cfg, AIHandler handler, const char *user_input, const char *sys_prompt, FILE *outf);

#endif
