#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ai.h"
#include "ai_core/agent.h"
#include "ai_core/core.h"
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

static char* invokeAgentTool(const char *name, const char *input, char **error_out) {
    if (!name) {
        if (error_out) *error_out = duplicateString("No tool name provided by the agent.");
        return NULL;
    }

    size_t toolCount = 0;
    const AgentTool *tools = getAgentTools(&toolCount);

    for (size_t i = 0; i < toolCount; ++i) {
        if (strcmp(name, tools[i].name) == 0) {
            return tools[i].invoke(input, error_out);
        }
    }

    if (error_out) *error_out = formatString("Unknown tool '%s'.", name);
    return NULL;
}

static char* buildAgentSystemPrompt(const char *sys_prompt) {
    const char *header =
        "You are an autonomous AI agent. Respond exclusively in JSON with keys: status, message, tool, toolInput.\n"
        "When status is \"continue\" you must provide tool and toolInput.\n"
        "Available tools:\n";
    const char *footer =
        "Use tools when needed. When you can answer the user, return status \"done\" and omit tool/toolInput.\n";

    size_t toolCount = 0;
    const AgentTool *tools = getAgentTools(&toolCount);

    size_t total = strlen(header) + strlen(footer) + 1;
    if (sys_prompt && *sys_prompt) {
        total += strlen(sys_prompt) + 2;
    }

    for (size_t i = 0; i < toolCount; ++i) {
        total += strlen(tools[i].name) + strlen(tools[i].description) + 6;
    }

    char *prompt = malloc(total);
    if (!prompt) {
        return NULL;
    }

    char *ptr = prompt;
    size_t remaining = total;

    if (sys_prompt && *sys_prompt) {
        size_t len = strlen(sys_prompt);
        memcpy(ptr, sys_prompt, len);
        ptr += len;
        remaining -= len;
        if (*(ptr - 1) != '\n') {
            *ptr++ = '\n';
            remaining--;
        }
    }

    int written = snprintf(ptr, remaining, "%s", header);
    if (written < 0 || (size_t)written >= remaining) {
        free(prompt);
        return NULL;
    }
    ptr += written;
    remaining -= (size_t)written;

    for (size_t i = 0; i < toolCount; ++i) {
        written = snprintf(ptr, remaining, "- %s: %s\n", tools[i].name, tools[i].description);
        if (written < 0 || (size_t)written >= remaining) {
            free(prompt);
            return NULL;
        }
        ptr += written;
        remaining -= (size_t)written;
    }

    written = snprintf(ptr, remaining, "%s", footer);
    if (written < 0 || (size_t)written >= remaining) {
        free(prompt);
        return NULL;
    }

    return prompt;
}

int runAgentMode(AIConfig *cfg, AIHandler handler, const char *user_input, const char *sys_prompt, FILE *outf) {
    char *agent_prompt = buildAgentSystemPrompt(sys_prompt);
    if (!agent_prompt) {
        return 1;
    }

    const char *initial = user_input ? user_input : "";
    char *conversation = duplicateString(initial);
    if (!conversation) {
        free(agent_prompt);
        return 1;
    }

    const int max_steps = 8;
    for (int step = 0; step < max_steps; ++step) {
        char *raw_json = NULL;
        char *response = NULL;
        int ret = callAiOnce(cfg, handler, conversation, agent_prompt, &raw_json, &response);
        if (ret != 0) {
            if (raw_json) free(raw_json);
            if (response) free(response);
            free(conversation);
            free(agent_prompt);
            return ret;
        }

        if (cfg->verbose && raw_json) {
            fprintf(stderr, "[agent][step %d] %s\n", step + 1, raw_json);
        }

        if (!response) {
            if (!cfg->verbose && raw_json) {
                fprintf(outf, "%s", raw_json);
            }
            if (raw_json) free(raw_json);
            free(conversation);
            free(agent_prompt);
            return 0;
        }

        char *status = find_json_string(response, "status");
        char *message = find_json_string(response, "message");

        if (!status) {
            const char *final_text = message ? message : response;
            fprintf(outf, "%s\n", final_text);
            if (message) free(message);
            free(response);
            if (raw_json) free(raw_json);
            free(conversation);
            free(agent_prompt);
            return 0;
        }

        int statusIsContinue = strcmp(status, "continue") == 0;
        int statusIsDone = strcmp(status, "done") == 0;

        if (statusIsContinue) {
            char *tool_name = find_json_string(response, "tool");
            char *tool_input = find_json_string(response, "toolInput");

            if (cfg->agentThinking) {
                if (message && *message) {
                    fprintf(stderr, "[agent][thinking] %s\n", message);
                } else if (tool_name && *tool_name) {
                    fprintf(stderr, "[agent][thinking] continuing with tool '%s'.\n", tool_name);
                } else {
                    fprintf(stderr, "[agent][thinking] continuing without a message from the agent.\n");
                }
            }

            char *tool_error = NULL;
            char *tool_output = NULL;
            if (tool_name) {
                tool_output = invokeAgentTool(tool_name, tool_input ? tool_input : "", &tool_error);
            } else {
                tool_error = duplicateString("Agent response missing tool name.");
            }

            const char *fallback = "Tool produced no output.";
            const char *tool_text = tool_output ? tool_output : (tool_error ? tool_error : fallback);
            size_t tool_text_len = tool_output ? strlen(tool_output) : (tool_error ? strlen(tool_error) : strlen(fallback));
            size_t new_len = strlen(conversation) + strlen(response) + (message ? strlen(message) : 0) + (tool_name ? strlen(tool_name) : 0) + (tool_input ? strlen(tool_input) : 0) + tool_text_len + 256;

            char *updated_conversation = malloc(new_len);
            if (!updated_conversation) {
                if (tool_output) free(tool_output);
                if (tool_error) free(tool_error);
                if (tool_name) free(tool_name);
                if (tool_input) free(tool_input);
                if (message) free(message);
                free(status);
                free(response);
                if (raw_json) free(raw_json);
                free(conversation);
                free(agent_prompt);
                return 1;
            }

            int written = snprintf(
                updated_conversation,
                new_len,
                "%s\n\n[agent step %d]\nResponse: %s\nMessage: %s\nTool: %s\nToolInput: %s\nToolOutput:\n%s\n\nContinue responding in JSON with keys status, message, tool, toolInput.",
                conversation,
                step + 1,
                response,
                message ? message : "(none)",
                tool_name ? tool_name : "(missing)",
                tool_input ? tool_input : "(empty)",
                tool_text_len ? tool_text : "(empty)"
            );

            if (written < 0 || (size_t)written >= new_len) {
                free(updated_conversation);
                if (tool_output) free(tool_output);
                if (tool_error) free(tool_error);
                if (tool_name) free(tool_name);
                if (tool_input) free(tool_input);
                if (message) free(message);
                free(status);
                free(response);
                if (raw_json) free(raw_json);
                free(conversation);
                free(agent_prompt);
                return 1;
            }

            free(conversation);
            conversation = updated_conversation;

            if (tool_output) free(tool_output);
            if (tool_error) {
                fprintf(stderr, "Agent tool '%s' error: %s\n", tool_name ? tool_name : "(unknown)", tool_error);
                free(tool_error);
            }

            if (tool_name) free(tool_name);
            if (tool_input) free(tool_input);
            if (message) free(message);
            free(status);
            free(response);
            if (raw_json) free(raw_json);
            continue;
        }

        if (cfg->agentThinking && message && *message && !statusIsDone) {
            fprintf(stderr, "[agent][thinking] %s\n", message);
        }

        if (statusIsDone) {
            const char *final_text = message ? message : response;
            fprintf(outf, "%s\n", final_text);
            if (message) free(message);
            free(status);
            free(response);
            if (raw_json) free(raw_json);
            free(conversation);
            free(agent_prompt);
            return 0;
        }

        const char *final_text = message ? message : response;
        fprintf(outf, "%s\n", final_text);
        if (message) free(message);
        free(status);
        free(response);
        if (raw_json) free(raw_json);
        free(conversation);
        free(agent_prompt);
        return 0;
    }

    fprintf(outf, "Agent stopped after maximum iterations without finishing.\n");
    free(conversation);
    free(agent_prompt);
    return 0;
}
