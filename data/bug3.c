#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This program reads book information from a file specified by the user.
 * Each line in the file is expected to have the format: "TITLE|AUTHOR|YEAR"
 *
 * For example:
 *   The Great Gatsby|F. Scott Fitzgerald|1925
 *   Nineteen Eighty-Four|George Orwell|1949
 *
 * It stores these books in a dynamically growing array and, at the end, computes:
 *   - The average publication year of all books.
 *   - Prints the details of the first book.
 *
 * The code looks like a real-world scenario: reading a database of books from a file.
 *
 * The subtle bugs:
 * - If any line doesn't have all three fields (TITLE, AUTHOR, YEAR), no book is added.
 * - If no valid books are processed, book_count stays 0, causing division by zero
 *   when computing the average publication year.
 * - Also, it prints the first book's info unconditionally, leading to out-of-bound array access
 *   if no books were added.
 * - If the YEAR field isn't numeric or is extremely large, atoi could return strange values,
 *   potentially causing integer overflow in the sum.
 * - The dynamic array growth logic is simple, but if the file contains certain malformed lines,
 *   repeated failures to add might cause unpredictable states.
 *
 * AFL can find these bugs easily, as providing empty or malformed input will trigger division by zero
 * or out-of-bound memory access within seconds.
 *
 * Usage:
 *   ./prog books.txt
 */

#define MAX_LINE 1024

struct book {
    char *title;
    char *author;
    int year;
};

static struct book *books = NULL;
static int book_count = 0;
static int book_capacity = 0;

static void add_book(const char *title, const char *author, int year) {
    if (book_count >= book_capacity) {
        int new_capacity = (book_capacity == 0) ? 2 : book_capacity * 2;
        struct book *new_books = realloc(books, (size_t)new_capacity * sizeof(struct book));
        if (!new_books) {
            // Ignore allocation failures
            return;
        }
        books = new_books;
        book_capacity = new_capacity;
    }

    books[book_count].title = strdup(title);
    books[book_count].author = strdup(author);
    books[book_count].year = year;
    book_count++;
}

static void parse_line(char *line) {
    // Expect: "TITLE|AUTHOR|YEAR"
    char *fields[3];
    int i = 0;

    char *saveptr = NULL;
    char *token = strtok_r(line, "|", &saveptr);
    while (token && i < 3) {
        fields[i++] = token;
        token = strtok_r(NULL, "|", &saveptr);
    }

    // If fewer than 3 fields, malformed line - no book added
    if (i < 3) {
        return;
    }

    // Trim spaces
    for (int j = 0; j < 3; j++) {
        while (*fields[j] == ' ' || *fields[j] == '\t') fields[j]++;
    }

    int year = atoi(fields[2]);
    add_book(fields[0], fields[1], year);
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

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        parse_line(line);
    }

    fclose(in);

    // Compute average publication year
    // If no books were processed (book_count == 0), division by zero occurs.
    long total_year = 0;
    for (int i = 0; i < book_count; i++) {
        total_year += books[i].year; // potential overflow if year is huge
    }

    long avg_year = total_year / book_count; // division by zero if book_count == 0
    printf("Loaded %d books. Average year: %ld\n", book_count, avg_year);

    // Print first book without checking if any book exists
    // If book_count == 0, out-of-bounds access on books[0].
    printf("First book: \"%s\" by %s (%d)\n", books[0].title, books[0].author, books[0].year);

    // Free memory
    for (int i = 0; i < book_count; i++) {
        free(books[i].title);
        free(books[i].author);
    }
    free(books);

    return 0;
}
