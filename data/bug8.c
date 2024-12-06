#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * This C program reads a binary file format that contains a series of "documents".
 * The file format (not a real standard, but a plausible scenario):
 *
 *   [4 bytes: number_of_documents (unsigned 32-bit)]
 *   For each document:
 *     [4 bytes: doc_length (unsigned 32-bit)]
 *     [doc_length bytes: doc_data]
 *
 * After reading all documents, the program:
 * - Computes the average document length and prints it.
 * - Prints the first document's data.
 *
 * A simple, well-formed input (like a file containing 1 small doc) won't crash:
 *   Example (hex):
 *     01 00 00 00   (1 doc)
 *     05 00 00 00   (length=5)
 *     48 65 6c 6c 6f (data = "Hello")
 * This should produce no immediate crash and print reasonable output.
 *
 * Subtle bugs that AFL can find:
 * - If number_of_documents is huge, multiplication when allocating arrays may overflow.
 * - If doc_length is extremely large, attempts to read doc_data can fail or cause partial reads,
 *   leading to buffer overflow or use of uninitialized memory.
 * - If doc_length calculations overflow, fewer bytes may be allocated than needed, causing 
 *   out-of-bound writes.
 * - If no documents are read successfully (e.g., malformed data), division by zero occurs
 *   when computing average length.
 * - If file truncation or unexpected values occur, partial reads can lead to index or pointer issues.
 * 
 * The bug is not obvious: code looks like a straightforward binary parser,
 * but doesn't strictly validate fields, leading to subtle memory and arithmetic errors 
 * under certain fuzzed inputs.
 *
 * Usage:
 *   ./prog inputfile
 */

struct Document {
    uint8_t *data;
    uint32_t length;
    int allocated; // 1 if allocated, 0 if not
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

    uint32_t doc_count = 0;
    if (fread(&doc_count, 4, 1, f) < 1) {
        // Not enough data for even the header
        fclose(f);
        return 0; // no crash
    }

    // Potential overflow if doc_count is huge
    size_t alloc_count = (size_t)doc_count;
    // If doc_count is extremely large, multiplying by sizeof(struct Document) might overflow.
    // We'll just trust doc_count and allocate. If overflow occurs, pairs is too small.
    struct Document *docs = malloc(alloc_count * sizeof(struct Document));
    if (!docs && doc_count > 0) {
        // allocation failure
        fclose(f);
        return 0; // no immediate crash, just stop
    }

    for (size_t i = 0; i < doc_count; i++) {
        docs[i].data = NULL;
        docs[i].length = 0;
        docs[i].allocated = 0;
    }

    // Read each document
    for (size_t i = 0; i < doc_count; i++) {
        uint32_t length;
        if (fread(&length, 4, 1, f) < 1) {
            // Not enough data for doc_length, stop reading
            break;
        }

        // If length is huge, multiplication by sizeof(uint8_t)=1 might overflow 
        // but not really. Could still be huge and cause failed malloc.
        uint8_t *buf = malloc(length);
        if (!buf && length > 0) {
            // allocation failed
            break; // just stop reading more
        }

        // Attempt to read length bytes
        if (fread(buf, 1, length, f) < length) {
            // partial read
            // We'll still store what we got. If length was large and we read fewer bytes, 
            // data might be incomplete and lead to uninitialized memory usage.
        }

        docs[i].data = buf;
        docs[i].length = length;
        docs[i].allocated = 1;
    }

    fclose(f);

    // Compute average length
    // If no docs allocated successfully (doc_count might be >0, but maybe we broke early),
    // then division by zero occurs.
    uint64_t total_len = 0;
    size_t actual_count = 0;
    for (size_t i = 0; i < doc_count; i++) {
        if (docs[i].allocated) {
            total_len += docs[i].length; // might overflow if lengths are huge
            actual_count++;
        }
    }

    if (actual_count == 0) {
        // Division by zero if we do total_len/actual_count
        // On simple input with 1 doc, actual_count=1 so no immediate crash.
        uint64_t avg = total_len / actual_count; // division by zero crash scenario
        printf("Average length: %llu\n", (unsigned long long)avg);
    } else {
        uint64_t avg = total_len / actual_count;
        printf("Average length: %llu\n", (unsigned long long)avg);
    }

    // Print first doc's data if any
    if (actual_count > 0) {
        // Find first allocated doc. If doc_count was large but none allocated properly,
        // actual_count>0 won't hold, so no immediate crash.
        for (size_t i = 0; i < doc_count; i++) {
            if (docs[i].allocated) {
                // Print up to 100 chars of the doc to avoid huge output
                size_t to_print = docs[i].length < 100 ? docs[i].length : 100;
                // If docs[i].data is NULL for some reason, this will crash. 
                // On simple correct input, data isn't NULL.
                fwrite(docs[i].data, 1, to_print, stdout);
                putchar('\n');
                break;
            }
        }
    }

    // Free
    for (size_t i = 0; i < doc_count; i++) {
        if (docs[i].allocated) {
            free(docs[i].data);
        }
    }
    free(docs);

    return 0;
}
