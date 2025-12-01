#include "mica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// forward declaration
extern void register_builtins(VM* vm);

// read file
static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "could not open file: %s\n", path);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);
    
    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    size_t read = fread(buffer, 1, size, file);
    buffer[read] = '\0';
    
    fclose(file);
    return buffer;
}

// repl
static void repl(VM* vm) {
    char line[1024];
    
    printf("mica 2.0 repl\n");
    printf("type 'exit' to quit\n\n");
    
    for (;;) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        
        // trim newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        if (strcmp(line, "exit") == 0) {
            break;
        }
        
        if (strlen(line) == 0) {
            continue;
        }
        
        if (mica_compile(vm, line)) {
            mica_run(vm);
        }
    }
}

// run file
static void run_file(VM* vm, const char* path) {
    char* source = read_file(path);
    if (!source) {
        exit(1);
    }
    
    if (!mica_compile(vm, source)) {
        fprintf(stderr, "compilation failed\n");
        free(source);
        exit(1);
    }
    
    if (!mica_run(vm)) {
        fprintf(stderr, "runtime error\n");
        free(source);
        exit(1);
    }
    
    free(source);
}

// main
int main(int argc, char** argv) {
    VM* vm = mica_new();
    register_builtins(vm);
    
    if (argc > 1) {
        run_file(vm, argv[1]);
    } else {
        repl(vm);
    }
    
    mica_free(vm);
    return 0;
}