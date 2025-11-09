
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ai.h"


static void usage(void) {
    fprintf(stderr, "Usage: gipwrap [options]\n");
    fprintf(stderr, "  -a  AI type (chatgpt|ollama|claude|deepseek) [default: chatgpt]\n");
    fprintf(stderr, "  -i  Input file [default: stdin]\n");
    fprintf(stderr, "  -o  Output file [default: stdout]\n");
    fprintf(stderr, "  -s  System prompt file\n");
    fprintf(stderr, "  -S  System prompt string\n");
    fprintf(stderr, "  -m  Model name\n");
    fprintf(stderr, "  -k  API key env variable name\n");
    fprintf(stderr, "  -K  API key raw\n");
    fprintf(stderr, "  -v  Verbose output (full JSON)\n");
    fprintf(stderr, "  -A  Enable agent mode with tool usage\n");
    fprintf(stderr, "  -T  Print agent thinking messages to stderr\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    AIConfig cfg = {
        .ai_type = "chatgpt",
        .input_file = NULL,
        .output_file = NULL,
        .sys_prompt_file = NULL,
        .sys_prompt = NULL,
        .model = NULL,
        .key_env = NULL,
        .key_raw = NULL,
        .verbose = 0,
        .agentMode = 0,
        .agentThinking = 0
    };

    int opt;
    while ((opt = getopt(argc, argv, "a:i:o:s:S:m:k:K:vATh")) != -1) {
        switch (opt) {
            case 'a': cfg.ai_type = optarg; break;
            case 'i': cfg.input_file = optarg; break;
            case 'o': cfg.output_file = optarg; break;
            case 's': cfg.sys_prompt_file = optarg; break;
            case 'S': cfg.sys_prompt = optarg; break;
            case 'm': cfg.model = optarg; break;
            case 'k': cfg.key_env = optarg; break;
            case 'K': cfg.key_raw = optarg; break;
            case 'v': cfg.verbose = 1; break;
            case 'A': cfg.agentMode = 1; break;
            case 'T': cfg.agentThinking = 1; break;
            case 'h':
            default: usage();
        }
    }
    
    int ret = ai_execute(&cfg);
    return ret;
}
