#include <iostream>
#include <string>
#include <vector>

/*
 * This C++ program simulates reading configuration lines from stdin.
 * Each valid configuration line should look like:
 *
 *    CONFIG param=value
 *
 * For example:
 *    CONFIG max_connections=100
 *    CONFIG server_name=localhost
 *
 * The program:
 *  - Parses each line
 *  - If it starts with "CONFIG " and contains '=', extracts param and value
 *  - Stores them in a vector of entries
 *  - Also stores pointers (c_str()) of param strings in a separate vector for quick access
 *
 * Subtle bug:
 * - If no valid "CONFIG" lines are found (because input doesn't match the pattern),
 *   then no entries are added, and the paramPointers vector remains empty.
 * - At the end, the program tries to print paramPointers[0].
 * - If paramPointers is empty, this is out-of-bounds and leads to undefined behavior.
 *
 * This is not extremely obvious because:
 * - The code looks like a normal parser that tries to handle configuration lines.
 * - The bug comes from the assumption that there's at least one CONFIG line.
 * - AFL will easily trigger this by providing empty or malformed input (e.g., random binary data)
 *   which will cause no valid entries to be added and quickly lead to accessing paramPointers[0] 
 *   when it's empty.
 *
 * AFL should find this bug in well under 30 seconds, since even a single empty input line 
 * or any input that doesn't contain a valid "CONFIG param=value" will cause an OOB read.
 */

struct ConfigEntry {
    std::string param;
    std::string value;
};

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::vector<ConfigEntry> entries;
    std::vector<const char*> paramPointers; 

    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) break; // EOF or read error

        // Check if line starts with "CONFIG "
        if (line.size() > 7 && line.compare(0, 7, "CONFIG ") == 0) {
            // Extract the rest: "param=value"
            std::string rest = line.substr(7);
            size_t eq_pos = rest.find('=');
            if (eq_pos != std::string::npos && eq_pos > 0 && eq_pos < rest.size()-1) {
                std::string param = rest.substr(0, eq_pos);
                std::string val = rest.substr(eq_pos + 1);
                if (!param.empty() && !val.empty()) {
                    entries.push_back({param, val});
                    paramPointers.push_back(entries.back().param.c_str());
                }
            }
            // If no '=' found or param/value empty, we skip silently.
        } else {
            // Non-"CONFIG" lines are ignored.
        }
    }

    // At this point, if no valid CONFIG lines were read, entries is empty and paramPointers is empty.
    // Attempt to access paramPointers[0] will cause out-of-bounds read.
    // Even if a single random character was provided, it's very unlikely to form a correct line,
    // so paramPointers stays empty.
    if (!paramPointers.empty()) {
        std::cout << "First parameter: " << paramPointers[0] << "\n";
    } else {
        // If no config lines, we do something that causes a bug:
        // Access paramPointers[0] directly, which is OOB.
        std::cout << "First parameter: " << paramPointers[0] << "\n";
    }

    return 0;
}
