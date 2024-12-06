#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

/*
 * This C++ program simulates a simplistic "in-memory database" of integer arrays,
 * reading commands from stdin. It supports various commands to create, modify, and 
 * analyze arrays of integers:
 *
 *   CREATE_ARRAY <index> <count>
 *   FILL_ARRAY <index> <value>
 *   SPLICE_ARRAY <dest> <src> <offset> <count>
 *   JOIN_ARRAYS <new> <idx1> <idx2>
 *   FREE_ARRAY <index>
 *   COMPUTE_STAT <index>
 *   PRINT_ARRAY <index> <start> <end>
 *
 * The code is designed so that a single simple example input (like just one valid command)
 * won't immediately crash. For example:
 *   CREATE_ARRAY 0 10
 *   FILL_ARRAY 0 5
 *   COMPUTE_STAT 0
 * should work without immediate crashing.
 *
 * However, there are many subtle bugs that can be exposed when fuzzed by AFL:
 * - Large or negative counts can cause integer overflows in allocations, leading to 
 *   incorrect memory sizes and potential OOB writes/reads.
 * - If commands are given in tricky order or with invalid indices, arrays may be 
 *   accessed when not allocated, causing null dereferences.
 * - Division by zero can occur in COMPUTE_STAT if the array size ends up being zero 
 *   after certain operations.
 * - Repeated FREE_ARRAY calls can cause double free.
 * - Off-by-one errors or incorrect offset/count parameters in SPLICE_ARRAY and 
 *   PRINT_ARRAY can cause OOB reads or writes.
 * - JOIN_ARRAYS can cause huge allocations and overflows if sizes add up incorrectly.
 *
 * The key point is that a single simple, well-formed input might not cause an immediate crash,
 * but as AFL mutates inputs and produces more complex sequences, it will uncover numerous bugs.
 */

struct ArrayInfo {
    int *data;
    size_t size;
    bool allocated;
};

static std::vector<ArrayInfo> arrays; // dynamically grows based on max index seen

static void ensure_capacity(size_t idx) {
    if (idx >= arrays.size()) {
        arrays.resize(idx + 1, {NULL, 0, false});
    }
}

static void create_array(size_t idx, long count) {
    ensure_capacity(idx);
    // Free old if allocated
    if (arrays[idx].allocated) {
        free(arrays[idx].data);
        arrays[idx].data = NULL;
        arrays[idx].size = 0;
        arrays[idx].allocated = false;
    }

    if (count < 0) count = 100; // fallback to a reasonable default to avoid immediate crash
    size_t alloc_size = (size_t)count * sizeof(int);
    int *p = (int*)malloc(alloc_size);
    if (!p) {
        // allocation failure, just leave array not allocated
        return;
    }
    arrays[idx].data = p;
    arrays[idx].size = (size_t)count;
    arrays[idx].allocated = true;

    // Initialize memory to reduce immediate uninitialized usage crashes:
    // (Less likely to crash on a single simple input, but still dangerous when fuzzed)
    for (size_t i = 0; i < arrays[idx].size; i++) {
        arrays[idx].data[i] = 0;
    }
}

static void fill_array(size_t idx, long value) {
    if (idx >= arrays.size() || !arrays[idx].allocated) {
        // If not allocated, just print a warning and return (no immediate crash)
        std::cerr << "fill_array: array not allocated\n";
        return;
    }
    for (size_t i = 0; i < arrays[idx].size; i++) {
        arrays[idx].data[i] = (int)value;
    }
}

static void splice_array(size_t dest, size_t src, long offset, long count) {
    ensure_capacity(dest);
    ensure_capacity(src);
    if (!arrays[dest].allocated || !arrays[src].allocated) {
        std::cerr << "splice_array: one or both arrays not allocated\n";
        return;
    }

    // Check if offset and count are at least somewhat sane to avoid immediate crash:
    if (offset < 0 || count < 0 || (long)arrays[src].size < offset + count) {
        std::cerr << "splice_array: invalid offset/count\n";
        return;
    }

    long new_size = (long)arrays[dest].size + count;
    if (new_size < 0) new_size = 10; // fallback
    int *new_data = (int*)realloc(arrays[dest].data, (size_t)new_size * sizeof(int));
    if (!new_data) {
        // On failure do nothing
        std::cerr << "splice_array: realloc failed\n";
        return;
    }
    arrays[dest].data = new_data;

    for (long i = 0; i < count; i++) {
        arrays[dest].data[arrays[dest].size + i] = arrays[src].data[offset + i];
    }
    arrays[dest].size = (size_t)new_size;
}

static void join_arrays(size_t new_idx, size_t idx1, size_t idx2) {
    ensure_capacity(new_idx);
    ensure_capacity(idx1);
    ensure_capacity(idx2);

    if (!arrays[idx1].allocated || !arrays[idx2].allocated) {
        std::cerr << "join_arrays: one or both arrays not allocated\n";
        return;
    }

    // Check for overflow
    if (arrays[idx1].size > SIZE_MAX - arrays[idx2].size) {
        std::cerr << "join_arrays: size overflow\n";
        return;
    }
    size_t new_size = arrays[idx1].size + arrays[idx2].size;
    int *p = (int*)malloc(new_size * sizeof(int));
    if (!p) {
        std::cerr << "join_arrays: malloc failed\n";
        return;
    }

    memcpy(p, arrays[idx1].data, arrays[idx1].size * sizeof(int));
    memcpy(p + arrays[idx1].size, arrays[idx2].data, arrays[idx2].size * sizeof(int));

    if (arrays[new_idx].allocated) {
        free(arrays[new_idx].data);
    }
    arrays[new_idx].data = p;
    arrays[new_idx].size = new_size;
    arrays[new_idx].allocated = true;
}

static void free_array(size_t idx) {
    if (idx < arrays.size() && arrays[idx].allocated) {
        free(arrays[idx].data);
        arrays[idx].data = NULL;
        arrays[idx].size = 0;
        arrays[idx].allocated = false;
    } else {
        // If already freed or never allocated, just ignore to avoid immediate crash
        std::cerr << "free_array: array not allocated or invalid index\n";
    }
}

static void compute_stat(size_t idx) {
    if (idx >= arrays.size() || !arrays[idx].allocated) {
        std::cerr << "compute_stat: array not allocated\n";
        return;
    }

    if (arrays[idx].size == 0) {
        // Avoid immediate division by zero on simple input
        std::cerr << "compute_stat: array is empty\n";
        return;
    }

    long sum = 0;
    for (size_t i = 0; i < arrays[idx].size; i++) {
        sum += arrays[idx].data[i];
    }
    long avg = sum / (long)arrays[idx].size;
    std::cout << "Average: " << avg << "\n";
}

static void print_array(size_t idx, long start, long end) {
    if (idx >= arrays.size() || !arrays[idx].allocated) {
        std::cerr << "print_array: array not allocated\n";
        return;
    }

    if (start < 0 || end < start || (size_t)end >= arrays[idx].size) {
        std::cerr << "print_array: invalid range\n";
        return;
    }

    for (long i = start; i <= end; i++) {
        std::cout << arrays[idx].data[i] << " ";
    }
    std::cout << "\n";
}


int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::string line;
    while (true) {
        if(!std::getline(std::cin, line)) break;

        // Tokenize by space
        std::vector<std::string> tokens;
        {
            size_t start = 0;
            while (true) {
                size_t pos = line.find(' ', start);
                if (pos == std::string::npos) {
                    if (start < line.size())
                        tokens.push_back(line.substr(start));
                    break;
                } else {
                    tokens.push_back(line.substr(start, pos - start));
                    start = pos + 1;
                }
            }
        }

        if (tokens.empty()) continue;

        // Convert arguments to long safely
        auto toL = [&](size_t i)->long {
            if (i >= tokens.size()) return 0;
            return strtol(tokens[i].c_str(), NULL, 10);
        };

        std::string cmd = tokens[0];
        if (cmd == "CREATE_ARRAY" && tokens.size() > 2) {
            create_array((size_t)toL(1), toL(2));
        } else if (cmd == "FILL_ARRAY" && tokens.size() > 2) {
            fill_array((size_t)toL(1), toL(2));
        } else if (cmd == "SPLICE_ARRAY" && tokens.size() > 4) {
            splice_array((size_t)toL(1), (size_t)toL(2), toL(3), toL(4));
        } else if (cmd == "JOIN_ARRAYS" && tokens.size() > 3) {
            join_arrays((size_t)toL(1), (size_t)toL(2), (size_t)toL(3));
        } else if (cmd == "FREE_ARRAY" && tokens.size() > 1) {
            free_array((size_t)toL(1));
        } else if (cmd == "COMPUTE_STAT" && tokens.size() > 1) {
            compute_stat((size_t)toL(1));
        } else if (cmd == "PRINT_ARRAY" && tokens.size() > 3) {
            print_array((size_t)toL(1), toL(2), toL(3));
        } else {
            // Unknown command or not enough arguments: 
            // Just ignore to avoid immediate crash on trivial input.
        }
    }

    // Cleanup all arrays
    for (size_t i = 0; i < arrays.size(); i++) {
        if (arrays[i].allocated) {
            free(arrays[i].data);
        }
    }

    return 0;
}
