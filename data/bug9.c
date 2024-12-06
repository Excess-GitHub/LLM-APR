#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * This C program reads a binary file describing a simple directed graph:
 * 
 * File format:
 *   [4 bytes: node_count (uint32_t)]
 *   [4 bytes: edge_count (uint32_t)]
 *   Then edge_count pairs of 4-byte values: (src_node, dst_node)
 *
 * After reading:
 *   - Allocates an adjacency list for each node.
 *   - Stores edges in those adjacency lists.
 *   - Finally, computes the average out-degree of all nodes and prints it.
 *   - Prints the edges from the first node.
 *
 * A simple well-formed input won't crash immediately:
 *   For example:
 *     node_count=2, edge_count=1
 *     edge: (0 -> 1)
 *   should produce a reasonable output without immediate crashes.
 *
 * Subtle bugs:
 * - If node_count is huge, multiplying by pointer sizes can overflow when allocating adjacency lists.
 * - If edge_count is huge, reading all edges might cause memory issues or partial reads.
 * - If edges reference nodes out of range (e.g., src_node or dst_node >= node_count), we do OOB writes or reads.
 * - If no edges are successfully read, dividing by zero occurs when computing average out-degree.
 * - If node_count=0 or malformed, we might do invalid memory accesses.
 *
 * AFL can produce mutated inputs that cause large node counts, invalid edges, or truncated files,
 * leading to numerous subtle crashes within seconds.
 *
 * Usage:
 *   ./prog inputfile
 */

struct AdjList {
    uint32_t *neighbors;
    size_t capacity;
    size_t count;
};

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <inputfile>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Could not open file %s\n", argv[1]);
        return 1;
    }

    uint32_t node_count = 0;
    uint32_t edge_count = 0;
    if (fread(&node_count, 4, 1, f) < 1) {
        // Not enough data for node_count
        fclose(f);
        return 0;
    }
    if (fread(&edge_count, 4, 1, f) < 1) {
        // Not enough data for edge_count
        fclose(f);
        return 0;
    }

    // Allocate adjacency lists
    // If node_count is huge, node_count * sizeof(struct AdjList) might overflow.
    struct AdjList *graphs = malloc((size_t)node_count * sizeof(struct AdjList));
    if (!graphs && node_count > 0) {
        // Allocation failure
        fclose(f);
        return 0;
    }

    for (size_t i = 0; i < node_count; i++) {
        graphs[i].neighbors = NULL;
        graphs[i].capacity = 0;
        graphs[i].count = 0;
    }

    // Read edges
    // If edge_count is huge, or truncated, partial reads happen.
    for (uint32_t i = 0; i < edge_count; i++) {
        uint32_t src, dst;
        if (fread(&src, 4, 1, f) < 1) {
            // Not enough data for an edge
            break;
        }
        if (fread(&dst, 4, 1, f) < 1) {
            // Not enough data for full edge
            break;
        }

        // If src or dst >= node_count, OOB write when we do adjacency insert
        if (src >= node_count) {
            // Introduce subtle bug: try to write anyway
            // Potential OOB if src is invalid
            size_t idx = (size_t)src; 
            // Attempt some weird indexing to cause OOB if mutated by AFL
            graphs[idx].neighbors = malloc(10 * sizeof(uint32_t));
            graphs[idx].neighbors[20] = dst; // OOB access
            // Just continue, messing memory
            continue;
        }

        // Insert dst into src's adjacency list
        struct AdjList *list = &graphs[src];
        if (list->count >= list->capacity) {
            size_t new_cap = (list->capacity == 0) ? 4 : list->capacity * 2;
            uint32_t *new_arr = realloc(list->neighbors, new_cap * sizeof(uint32_t));
            if (!new_arr) {
                // On fail, do nothing - might leave list half-functional
                continue;
            }
            list->neighbors = new_arr;
            list->capacity = new_cap;
        }
        list->neighbors[list->count++] = dst;
    }

    fclose(f);

    // Compute average out-degree
    // If actual edges read is zero, division by zero occurs.
    uint64_t total_edges = 0;
    for (size_t i = 0; i < node_count; i++) {
        total_edges += graphs[i].count; // sum might overflow on huge counts
    }

    if (node_count == 0) {
        // Division by zero if node_count=0
        uint64_t avg = total_edges / (uint64_t)node_count; // Crash scenario
        printf("Average out-degree: %llu\n", (unsigned long long)avg);
    } else {
        uint64_t avg = total_edges / (uint64_t)node_count;
        printf("Average out-degree: %llu\n", (unsigned long long)avg);
    }

    // Print first node's edges if any node exists
    if (node_count > 0 && graphs[0].neighbors != NULL) {
        printf("Edges from node 0:");
        for (size_t i = 0; i < graphs[0].count; i++) {
            // If count is large or invalid, OOB read
            printf(" %u", graphs[0].neighbors[i]);
        }
        printf("\n");
    }

    // Free everything
    for (size_t i = 0; i < node_count; i++) {
        free(graphs[i].neighbors);
    }
    free(graphs);

    return 0;
}
