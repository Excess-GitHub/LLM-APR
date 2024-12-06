#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

/*
 * This C program simulates a "record manager" that stores arrays of integers 
 * (records) and supports several commands to manipulate these arrays. 
 * It's designed for fuzzing with AFL and has numerous subtle bugs that 
 * can produce dozens or even hundreds of unique crashes under fuzzing.
 *
 * Requirements:
 * - Must not crash trivially from a single simple input. For instance, just 
 *   "CREATE 0 10" followed by "FILL 0 1" and "PRINT 0 0 9" should work fine.
 * - Over time, as AFL mutates inputs to produce complex or malformed commands 
 *   (huge indexes, negative sizes, invalid commands), various memory and logic 
 *   bugs will emerge.
 *
 * Commands:
 *   CREATE <index> <size>
 *     - Creates a new array at 'index' with 'size' elements.
 *   FILL <index> <value>
 *     - Fills the array at 'index' with 'value'.
 *   SPLICE <dest> <src> <offset> <count>
 *     - Appends a slice [offset, offset+count-1] from 'src' array to 'dest' array.
 *   JOIN <new> <idx1> <idx2>
 *     - Creates a new array at 'new' by joining arrays at idx1 and idx2.
 *   FREE <index>
 *     - Frees the array at 'index'.
 *   STAT <index>
 *     - Prints average of elements in array at 'index'.
 *   PRINT <index> <start> <end>
 *     - Prints elements of array[index] from 'start' to 'end'.
 *
 * Subtle Bugs:
 * - Large or negative sizes cause integer overflows in allocation calculations.
 * - Repeated FREE can cause double frees.
 * - SPLICE, JOIN, PRINT can cause out-of-bounds reads/writes if 'offset', 'count', 'start', 'end' 
 *   are invalid or if size calculations overflow.
 * - STAT can cause division by zero if array size is zero.
 * - Some fallback logic attempts to avoid immediate crashes, but doesn't cover all cases, 
 *   allowing AFL to discover many different crash states.
 *
 * This code looks somewhat reasonable, but lacks thorough validation and is 
 * susceptible to complex malformed inputs. Simple correct commands won't 
 * cause an immediate crash, but fuzzed mutated inputs will find plenty of bugs.
 */


struct Array {
    int *data;
    size_t size;
    int allocated; // 0 or 1
};

static struct Array *arrays = NULL;
static size_t array_count = 0;

static void ensure_capacity(size_t idx) {
    if (idx >= array_count) {
        size_t new_count = idx + 1;
        // Avoid immediate huge allocations on a single step:
        if (new_count < array_count * 2)
            new_count = array_count * 2 + 1;
        if (new_count < 16)
            new_count = 16;

        struct Array *new_arrays = realloc(arrays, new_count * sizeof(struct Array));
        if (!new_arrays) {
            // Allocation failed, do nothing (just ignore)
            return;
        }
        for (size_t i = array_count; i < new_count; i++) {
            new_arrays[i].data = NULL;
            new_arrays[i].size = 0;
            new_arrays[i].allocated = 0;
        }
        arrays = new_arrays;
        array_count = new_count;
    }
}

static void create_array(size_t idx, long size_arg) {
    ensure_capacity(idx);
    if (idx >= array_count) {
        // could not expand arrays
        return;
    }

    // If already allocated, free it first (may not be a bug here, but can cause 
    // double frees if corrupted states appear later)
    if (arrays[idx].allocated) {
        free(arrays[idx].data);
        arrays[idx].data = NULL;
        arrays[idx].allocated = 0;
        arrays[idx].size = 0;
    }

    if (size_arg < 0) size_arg = 10; // fallback
    // Potential overflow:
    // If size_arg is huge, multiplication by sizeof(int) may overflow.
    size_t count = (size_t)size_arg;
    if (count > SIZE_MAX / sizeof(int)) {
        // If overflow would occur, fallback to a smaller size
        count = 100;
    }

    int *p = (int*)malloc(count * sizeof(int));
    if (!p) {
        // allocation failed
        return;
    }

    arrays[idx].data = p;
    arrays[idx].size = count;
    arrays[idx].allocated = 1;

    // Initialize to avoid immediate usage of uninitialized memory
    for (size_t i = 0; i < count; i++)
        p[i] = 0;
}

static void fill_array(size_t idx, long value) {
    if (idx >= array_count || !arrays[idx].allocated) {
        // Just ignore if not allocated to avoid immediate crash
        return;
    }

    for (size_t i = 0; i < arrays[idx].size; i++) {
        arrays[idx].data[i] = (int)value;
    }
}

static void splice_array(size_t dest, size_t src, long offset, long count) {
    if (dest >= array_count || src >= array_count) {
        return;
    }
    if (!arrays[dest].allocated || !arrays[src].allocated) {
        return;
    }

    if (offset < 0 || count < 0) {
        // fallback: do nothing, but still can fail if fuzz sets huge values
        return;
    }

    long end = offset + count;
    if ((long)arrays[src].size < end) {
        // count too large, do nothing
        return;
    }

    // Calculate new size
    // Could overflow if arrays[dest].size + count is huge
    size_t new_size = arrays[dest].size + (size_t)count;
    if (new_size < arrays[dest].size) {
        // Overflow happened, fallback to a smaller size
        new_size = arrays[dest].size + 10;
    }

    int *new_data = realloc(arrays[dest].data, new_size * sizeof(int));
    if (!new_data) {
        return;
    }
    arrays[dest].data = new_data;

    for (long i = 0; i < count; i++) {
        arrays[dest].data[arrays[dest].size + i] = arrays[src].data[offset + i];
    }

    arrays[dest].size = new_size;
}

static void join_arrays(size_t new_idx, size_t idx1, size_t idx2) {
    if (idx1 >= array_count || idx2 >= array_count) {
        return;
    }
    if (!arrays[idx1].allocated || !arrays[idx2].allocated) {
        return;
    }

    size_t new_size = arrays[idx1].size + arrays[idx2].size;
    if (new_size < arrays[idx1].size || new_size < arrays[idx2].size) {
        // overflow
        new_size = arrays[idx1].size + arrays[idx2].size / 2;
    }

    ensure_capacity(new_idx);
    if (new_idx >= array_count) {
        return;
    }

    // If already allocated at new_idx, free it
    if (arrays[new_idx].allocated) {
        free(arrays[new_idx].data);
    }

    int *p = (int*)malloc(new_size * sizeof(int));
    if (!p) return;

    memcpy(p, arrays[idx1].data, arrays[idx1].size * sizeof(int));
    memcpy(p + arrays[idx1].size, arrays[idx2].data, arrays[idx2].size * sizeof(int));

    arrays[new_idx].data = p;
    arrays[new_idx].size = new_size;
    arrays[new_idx].allocated = 1;
}

static void free_array(size_t idx) {
    if (idx < array_count && arrays[idx].allocated) {
        free(arrays[idx].data);
        arrays[idx].data = NULL;
        arrays[idx].size = 0;
        arrays[idx].allocated = 0;
    }
    // If not allocated, do nothing to avoid immediate trivial crash
}

static void compute_stat(size_t idx) {
    if (idx >= array_count || !arrays[idx].allocated) {
        // no array, do nothing
        return;
    }
    if (arrays[idx].size == 0) {
        // no division by zero on simple input
        return;
    }
    long sum = 0;
    for (size_t i = 0; i < arrays[idx].size; i++) {
        sum += arrays[idx].data[i];
    }
    long avg = sum / (long)arrays[idx].size;
    printf("Average: %ld\n", avg);
}

static void print_array(size_t idx, long start, long end) {
    if (idx >= array_count || !arrays[idx].allocated) {
        return;
    }
    if (start < 0 || end < start || (size_t)end >= arrays[idx].size) {
        // Avoid immediate crash on simple input
        return;
    }

    for (long i = start; i <= end; i++) {
        printf("%d ", arrays[idx].data[i]);
    }
    printf("\n");
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        // remove newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] == '\0') continue;

        // tokenize
        char *tokens[10];
        int count = 0;
        char *saveptr = NULL;
        char *tok = strtok_r(line, " ", &saveptr);
        while (tok && count < 10) {
            tokens[count++] = tok;
            tok = strtok_r(NULL, " ", &saveptr);
        }
        if (count == 0) continue;

        // Convert args to long helper
        #define ARG_L(i) ((i) < count ? strtol(tokens[i], NULL, 10) : 0)

        if (strcmp(tokens[0], "CREATE") == 0 && count > 2) {
            create_array((size_t)ARG_L(1), ARG_L(2));
        } else if (strcmp(tokens[0], "FILL") == 0 && count > 2) {
            fill_array((size_t)ARG_L(1), ARG_L(2));
        } else if (strcmp(tokens[0], "SPLICE") == 0 && count > 4) {
            splice_array((size_t)ARG_L(1), (size_t)ARG_L(2), ARG_L(3), ARG_L(4));
        } else if (strcmp(tokens[0], "JOIN") == 0 && count > 3) {
            join_arrays((size_t)ARG_L(1), (size_t)ARG_L(2), (size_t)ARG_L(3));
        } else if (strcmp(tokens[0], "FREE") == 0 && count > 1) {
            free_array((size_t)ARG_L(1));
        } else if (strcmp(tokens[0], "STAT") == 0 && count > 1) {
            compute_stat((size_t)ARG_L(1));
        } else if (strcmp(tokens[0], "PRINT") == 0 && count > 3) {
            print_array((size_t)ARG_L(1), ARG_L(2), ARG_L(3));
        } else {
            // Unknown or insufficient args: just ignore
        }
    }

    // Cleanup
    for (size_t i = 0; i < array_count; i++) {
        if (arrays[i].allocated) {
            free(arrays[i].data);
        }
    }
    free(arrays);

    return 0;
}
