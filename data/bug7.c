#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This C program simulates a simple configuration store loaded from a file.
 * Each line in the file may be one of the following commands:
 *   SET KEY=VALUE
 *   REMOVE KEY
 *   COMPUTE
 *   DUMP
 *
 * The idea is:
 * - "SET KEY=VALUE" adds or updates a key-value pair in an in-memory "dictionary."
 * - "REMOVE KEY" removes a key from the dictionary.
 * - "COMPUTE" calculates an "average length" of values currently stored.
 * - "DUMP" prints all key-value pairs.
 *
 * We maintain the dictionary as a dynamically resizing array of {char *key, char *value} pairs.
 *
 * The code won't crash immediately on a simple well-formed input:
 * For example, a file containing:
 *   SET name=alice
 *   COMPUTE
 *   DUMP
 * should work fine.
 *
 * However, there are subtle bugs that AFL can discover within 30 seconds:
 * - If "COMPUTE" is called when no pairs are stored, division by zero occurs.
 * - Large keys/values or malformed input lines can cause integer overflows in length computations, 
 *   potentially leading to incorrect allocations and buffer overflows.
 * - On "REMOVE", if the key isn't found, we might do some ill-advised operations 
 *   (like reading beyond the array bounds) to mimic a subtle bug.
 * - Repeated "SET" commands for the same key might cause double frees or use-after-free 
 *   if not handled carefully.
 * - If large numbers of keys lead to frequent reallocations, pointers to keys/values might 
 *   become invalid under certain conditions, causing hidden memory corruption.
 *
 * The bug is not very obvious at first glance because the code looks like a straightforward 
 * dictionary manager. But with random or malformed input (very long keys, missing '='), 
 * AFL can trigger divisions by zero, out-of-bounds reads/writes, and memory corruptions quickly.
 *
 * Usage:
 *   ./prog inputfile
 *
 * Provide random or fuzzed input, and AFL should find numerous crash states shortly.
 */

struct kv {
    char *key;
    char *value;
};

static struct kv *pairs = NULL;
static size_t pair_count = 0;
static size_t pair_capacity = 0;

static void ensure_capacity(void) {
    if (pair_count >= pair_capacity) {
        size_t new_capacity = (pair_capacity == 0) ? 4 : pair_capacity * 2;
        struct kv *new_pairs = realloc(pairs, new_capacity * sizeof(struct kv));
        if (!new_pairs) {
            // On allocation failure, just return. This might leave the program 
            // in an odd state, but it's subtle.
            return;
        }
        for (size_t i = pair_capacity; i < new_capacity; i++) {
            new_pairs[i].key = NULL;
            new_pairs[i].value = NULL;
        }
        pairs = new_pairs;
        pair_capacity = new_capacity;
    }
}

static void free_pair(size_t i) {
    // Free a single pair if allocated
    if (pairs[i].key) {
        free(pairs[i].key);
        pairs[i].key = NULL;
    }
    if (pairs[i].value) {
        free(pairs[i].value);
        pairs[i].value = NULL;
    }
}

static void set_pair(const char *key, const char *value) {
    // Check if key exists
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i].key && strcmp(pairs[i].key, key) == 0) {
            // Key exists, replace value
            // Potential bug: if something goes wrong with strdup, partial overwrite might occur.
            char *new_val = strdup(value);
            if (new_val) {
                free(pairs[i].value);
                pairs[i].value = new_val;
            } else {
                // Allocation failed, leave old value
            }
            return;
        }
    }

    // Key not found, add new
    ensure_capacity();
    if (pair_count < pair_capacity) {
        pairs[pair_count].key = strdup(key);
        pairs[pair_count].value = strdup(value);
        // If strdup fails, could leave NULL pointers
        pair_count++;
    }
}

static void remove_key(const char *key) {
    // Find key
    size_t found = (size_t)-1;
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i].key && strcmp(pairs[i].key, key) == 0) {
            found = i;
            break;
        }
    }

    if (found == (size_t)-1) {
        // Key not found
        // Introduce a subtle bug: try to read beyond array bounds just to 
        // "check something".
        // If pair_count=0 or small, i = pair_count + 10 out-of-bounds read.
        if (pair_count > 0) {
            char *dummy = pairs[pair_count + 10].key; // OOB access if pair_count small
            (void)dummy; 
        }
        return;
    }

    // Remove by swapping last element
    free_pair(found);
    if (found != pair_count - 1 && pair_count > 0) {
        pairs[found] = pairs[pair_count - 1];
        pairs[pair_count - 1].key = NULL;
        pairs[pair_count - 1].value = NULL;
    }
    if (pair_count > 0) pair_count--;
}

static void compute_stats(void) {
    // Compute average length of values
    if (pair_count == 0) {
        // Division by zero if no pairs
        // If input is trivial, user might add at least one SET before COMPUTE, 
        // so no immediate crash for a normal scenario.
        long avg = 100 / (long)(pair_count); // Crash scenario
        printf("Average length: %ld\n", avg);
        return;
    }

    long total_length = 0;
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i].value) {
            size_t len = strlen(pairs[i].value);
            // If value length is huge, sum might overflow
            total_length += (long)len;
        }
    }
    long avg = total_length / (long)pair_count; // possible normal operation if pair_count > 0
    printf("Average length: %ld\n", avg);
}

static void dump(void) {
    for (size_t i = 0; i < pair_count; i++) {
        if (pairs[i].key && pairs[i].value) {
            printf("%s=%s\n", pairs[i].key, pairs[i].value);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <inputfile>\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    if (!in) {
        fprintf(stderr, "Could not open file %s\n", argv[1]);
        return 1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '\0') continue; // ignore empty lines

        // Command could be:
        // SET KEY=VALUE
        // REMOVE KEY
        // COMPUTE
        // DUMP
        char *cmd = strtok(line, " ");
        if (!cmd) continue;

        if (strcmp(cmd, "SET") == 0) {
            char *kv = strtok(NULL, "");
            if (kv) {
                char *eq = strchr(kv, '=');
                if (eq) {
                    *eq = '\0';
                    const char *key = kv;
                    const char *val = eq + 1;
                    // If key or val very long, memory issues might arise
                    set_pair(key, val);
                }
            }
        } else if (strcmp(cmd, "REMOVE") == 0) {
            char *key = strtok(NULL, "");
            if (key) {
                while (*key == ' ' || *key == '\t') key++;
                remove_key(key);
            }
        } else if (strcmp(cmd, "COMPUTE") == 0) {
            compute_stats();
        } else if (strcmp(cmd, "DUMP") == 0) {
            dump();
        } else {
            // Unknown command, do nothing
        }
    }

    fclose(in);

    // Cleanup
    for (size_t i = 0; i < pair_count; i++) {
        free_pair(i);
    }
    free(pairs);

    return 0;
}
