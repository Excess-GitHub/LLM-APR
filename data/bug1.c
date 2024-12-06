#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This program attempts to parse commands from stdin and perform operations
 * based on them. It's intentionally buggy and somewhat complicated, suitable
 * for fuzzing with AFL. It only reads from stdin. 
 *
 * Potential issues:
 * - No proper input validation (could cause buffer overflows or invalid memory reads)
 * - Potential integer overflows with large input sizes
 * - Division by zero if specific input patterns are given
 * - Memory not always zero-initialized and indexing out of range is possible
 * - Unchecked memory allocations (if input length is too large)
 */

#define MAX_LINE 1024
#define MAX_TOKENS 16

static int process_command(char **tokens, int count) {
    // This function tries to do something "complex" with the given tokens.
    // We'll introduce a few logical and memory safety issues here.
    // tokens[0] might be a command, tokens[1] might be an integer, etc.
    // We'll assume at least one token is present.
    
    if (count < 1) return 0; // no tokens, do nothing

    // Let's say we have commands like:
    // "CALC <num1> <op> <num2>"
    // "ALLOC <size>"
    // "ECHO <str>"
    // We'll try to do some naive parsing and operations without checks.

    if (strcmp(tokens[0], "CALC") == 0) {
        // Expect something like: CALC 10 / 2
        if (count >= 4) {
            int a = atoi(tokens[1]);
            int b = atoi(tokens[3]);
            char op = tokens[2][0];

            int result = 0;
            switch (op) {
                case '+': result = a + b; break;
                case '-': result = a - b; break;
                case '*': result = a * b; break;
                case '/': result = a / b; break; // division by zero possible
                default: result = a; break;
            }
            printf("Result: %d\n", result);
        } else {
            // Missing tokens - might behave unexpectedly
            int *p = NULL;
            // Potential invalid memory read/write:
            // (This is obviously bad, but it's intentional for fuzzing.)
            printf("Bad calc command: %d\n", p[10]); 
        }
    } else if (strcmp(tokens[0], "ALLOC") == 0) {
        if (count >= 2) {
            int sz = atoi(tokens[1]);
            // No checks on sz, could be huge or negative (which leads to weird behavior).
            char *buf = malloc(sz);
            if (buf) {
                // We'll fill it with user data on the next line,
                // intentionally allowing possible buffer overflow.
                // It's not guaranteed we'll even have another line that big.
                if (fread(buf, 1, sz, stdin) != (size_t)sz) {
                    // Not enough data read - let's just print something from beyond the buffer
                    printf("Read incomplete data: %c\n", buf[sz+10]); // OOB access
                } else {
                    // Let's print something to cause potential issues if buf isn't null-terminated:
                    printf("Data read: %s\n", buf); // no guarantee buf has '\0'
                }
                free(buf);
            } else {
                // Could not allocate memory - do something silly
                char stack_buf[10];
                stack_buf[20] = 'X'; // OOB write on stack
            }
        } else {
            // No size given, let's try to do something weird:
            int arr[5];
            arr[10] = 42; // OOB write
        }
    } else if (strcmp(tokens[0], "ECHO") == 0) {
        // Just print all tokens except the command
        // If count is large, we don't do bounds checks
        for (int i = 1; i < count; i++) {
            printf("%s ", tokens[i]);
        }
        printf("\n");
    } else {
        // Unknown command: do something random
        char *junk = malloc(10);
        strcpy(junk, "HelloWorld!"); // OOB write on small buffer
        free(junk);
    }

    return 0;
}


int main(void) {
    char line[MAX_LINE];
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) {
            break; // EOF or read error
        }

        // Strip newline if present
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Split line into tokens
        char *tokens[MAX_TOKENS];
        int count = 0;
        char *saveptr = NULL;
        char *tok = strtok_r(line, " ", &saveptr);
        while (tok && count < MAX_TOKENS) {
            tokens[count++] = tok;
            tok = strtok_r(NULL, " ", &saveptr);
        }

        // Process the command line
        process_command(tokens, count);
    }

    return 0;
}
