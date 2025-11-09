#ifndef TOOLS_H
#define TOOLS_H

#include <stddef.h>

typedef char* (*AgentToolInvoker)(const char *input, char **error_out);

typedef struct {
    const char *name;
    const char *description;
    AgentToolInvoker invoke;
} AgentTool;

const AgentTool* getAgentTools(size_t *count);

#endif
