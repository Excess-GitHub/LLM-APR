#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * This C program reads a binary "record database" from a file. The file format:
 *
 *   [4 bytes: record_count (uint32_t)]
 *   For each record:
 *     [4 bytes: field_count (uint32_t)]
 *     For each field:
 *       [4 bytes: field_length (uint32_t)]
 *       [field_length bytes: field_data (not null-terminated)]
 *
 * After reading all records, the program:
 *   - Computes the average number of fields per record and prints it.
 *   - Prints the first record's first field data.
 *
 * A simple, well-formed input won't crash immediately. For example:
 *   record_count=1
 *     field_count=1
 *       field_length=5, field_data="Hello"
 * Works fine, prints average = 1, prints "Hello".
 *
 * Subtle bugs AFL can find within seconds:
 * - If record_count is huge, allocating arrays might overflow or fail, causing strange behavior.
 * - If field_count is huge or field_length is huge, multiplication and allocations can overflow,
 *   leading to improper buffer sizes and subsequent OOB writes.
 * - If no records are read successfully or records have zero fields, division by zero may occur
 *   when computing the average fields per record.
 * - Partial reads can cause uninitialized memory usage or attempts to read beyond allocated buffers.
 * - If field_length claims huge sizes but the file doesn't provide enough data, 
 *   reading partial data may leave arrays in unexpected states.
 *
 * The bug is not obvious because it looks like a normal file parser. But with fuzzed input 
 * (very large numbers, missing data), AFL will trigger many subtle crashes quickly.
 *
 * Usage:
 *   ./prog inputfile
 */

struct Field {
    uint8_t *data;
    uint32_t length;
    int allocated;
};

struct Record {
    struct Field *fields;
    uint32_t field_count;
    int allocated;
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

    uint32_t record_count = 0;
    if (fread(&record_count, 4, 1, f) < 1) {
        // Not enough data for even record count
        fclose(f);
        return 0;
    }

    // Potential overflow if record_count is huge
    struct Record *records = malloc((size_t)record_count * sizeof(struct Record));
    if (!records && record_count > 0) {
        fclose(f);
        return 0; // allocation failed
    }

    for (size_t i = 0; i < record_count; i++) {
        records[i].fields = NULL;
        records[i].field_count = 0;
        records[i].allocated = 0;
    }

    // Read each record
    for (size_t i = 0; i < record_count; i++) {
        uint32_t field_count = 0;
        if (fread(&field_count, 4, 1, f) < 1) {
            // Not enough data for field count
            break; 
        }

        // If field_count is huge, field array allocation may overflow
        struct Field *fields = malloc((size_t)field_count * sizeof(struct Field));
        if (!fields && field_count > 0) {
            // Allocation failed, no fields for this record
            continue;
        }

        for (size_t j = 0; j < field_count; j++) {
            fields[j].data = NULL;
            fields[j].length = 0;
            fields[j].allocated = 0;
        }

        int all_ok = 1;
        for (size_t j = 0; j < field_count; j++) {
            uint32_t length;
            if (fread(&length, 4, 1, f) < 1) {
                // Not enough data for field length
                all_ok = 0;
                break;
            }

            uint8_t *buf = malloc(length);
            if (!buf && length > 0) {
                // Allocation failed, skip reading field data
                all_ok = 0;
                break;
            }

            size_t read_bytes = fread(buf, 1, length, f);
            if (read_bytes < length) {
                // Partial read, still assign what we got
                // Might lead to uninitialized memory usage later
            }

            fields[j].data = buf;
            fields[j].length = length;
            fields[j].allocated = 1;
        }

        if (!all_ok) {
            // Free any fields allocated so far for this record
            for (size_t j = 0; j < field_count; j++) {
                if (fields[j].allocated) free(fields[j].data);
            }
            free(fields);
            // Move to next record
            continue;
        }

        records[i].fields = fields;
        records[i].field_count = field_count;
        records[i].allocated = 1;
    }

    fclose(f);

    // Compute average fields per record
    // If record_count>0 but all failed, we have zero allocated records and 
    // division by zero.
    uint64_t total_fields = 0;
    size_t allocated_count = 0;
    for (size_t i = 0; i < record_count; i++) {
        if (records[i].allocated) {
            total_fields += records[i].field_count; // could overflow on large counts
            allocated_count++;
        }
    }

    if (allocated_count == 0) {
        // Division by zero scenario
        uint64_t avg = total_fields / (uint64_t)allocated_count;
        printf("Average fields per record: %llu\n", (unsigned long long)avg);
    } else {
        uint64_t avg = total_fields / (uint64_t)allocated_count;
        printf("Average fields per record: %llu\n", (unsigned long long)avg);
    }

    // Print first record's first field if available
    if (allocated_count > 0) {
        // Find the first allocated record
        for (size_t i = 0; i < record_count; i++) {
            if (records[i].allocated && records[i].field_count > 0) {
                // Print up to 50 chars of the first field
                size_t to_print = (records[i].fields[0].length < 50) ? records[i].fields[0].length : 50;
                // If fields[0].data is NULL or something unexpected happened, crash possible
                fwrite(records[i].fields[0].data, 1, to_print, stdout);
                putchar('\n');
                break;
            }
        }
    }

    // Cleanup
    for (size_t i = 0; i < record_count; i++) {
        if (records[i].allocated) {
            for (size_t j = 0; j < records[i].field_count; j++) {
                if (records[i].fields[j].allocated)
                    free(records[i].fields[j].data);
            }
            free(records[i].fields);
        }
    }
    free(records);

    return 0;
}
