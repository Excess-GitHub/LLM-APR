#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This program reads user profiles from stdin in a CSV-like format:
 * Each line: "name,age,email"
 *
 * The program:
 *   - Parses each line into a name, an age, and an email.
 *   - Stores them in a dynamic array.
 *   - At the end, computes the average age and prints the first user's info.
 *
 * This is a plausible "real world" scenario: ingesting user data from a CSV.
 *
 * The bug is not very obvious:
 *   - If the input never provides three valid comma-separated fields, no users are added.
 *   - At the end, we compute "average = total_age / user_count" without checking if user_count > 0.
 *     This can cause a division by zero.
 *   - Also, we attempt to print the first user's info unconditionally, which leads to 
 *     out-of-bounds memory access if no users were parsed.
 *   
 * On a well-formed input, it may not crash:
 *   e.g.:
 *     alice,30,alice@example.com
 *     bob,25,bob@example.com
 *   This works fine.
 *
 * But with malformed or empty input (which AFL is likely to produce), 
 * user_count might remain 0, triggering division by zero or out-of-bounds access.
 *
 * The bug is subtle because everything looks standard, and there's no obvious 
 * "dangerous" function call. The error arises from an unchecked assumption that 
 * at least one user is present.
 *
 * AFL can quickly find these conditions because random or empty input 
 * will cause user_count=0 and lead to a crash within seconds.
 */

#define MAX_LINE 1024

struct user {
    char *name;
    int age;
    char *email;
};

static struct user *users = NULL;
static int user_count = 0;
static int user_capacity = 0;

static void add_user(const char *name, int age, const char *email) {
    if (user_count >= user_capacity) {
        int new_capacity = (user_capacity == 0) ? 2 : user_capacity * 2;
        struct user *new_users = realloc(users, (size_t)new_capacity * sizeof(struct user));
        if (!new_users) {
            // Just return on allocation failure
            return;
        }
        users = new_users;
        user_capacity = new_capacity;
    }

    users[user_count].name = strdup(name);
    users[user_count].email = strdup(email);
    users[user_count].age = age;
    user_count++;
}

static void parse_line(char *line) {
    // Expect: "name,age,email"
    char *fields[3];
    int i = 0;

    char *saveptr = NULL;
    char *token = strtok_r(line, ",", &saveptr);
    while (token && i < 3) {
        fields[i++] = token;
        token = strtok_r(NULL, ",", &saveptr);
    }

    // If fewer than 3 fields, malformed line, no user added
    if (i < 3) {
        return;
    }

    // Trim spaces
    for (int j = 0; j < 3; j++) {
        while (*fields[j] == ' ' || *fields[j] == '\t') fields[j]++;
    }

    int age = atoi(fields[1]); // no strict validation of age
    add_user(fields[0], age, fields[2]);
}

int main(void) {
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), stdin)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        parse_line(line);
    }

    // Compute stats:
    // If no users, user_count = 0, division by zero will occur here.
    long total_age = 0;
    for (int i = 0; i < user_count; i++) {
        total_age += users[i].age; // potential overflow if ages are huge
    }

    // No check if user_count > 0. If no users, this divides by zero.
    long average = total_age / user_count; 
    printf("Loaded %d users. Average age: %ld\n", user_count, average);

    // Print first user info without checking if user_count > 0
    // If no users, this is an out-of-bounds read.
    printf("First user: %s (%d) <%s>\n", users[0].name, users[0].age, users[0].email);

    // Free memory
    for (int i = 0; i < user_count; i++) {
        free(users[i].name);
        free(users[i].email);
    }
    free(users);

    return 0;
}
