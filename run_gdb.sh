#!/bin/bash

# Check if sufficient arguments are provided
if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <compiled_program_path> <input_type> <crash_dir> <num_crashes_to_try>"
    exit 1
fi

# Read arguments
COMPILED_PROGRAM_PATH="$1"
COMPILED_PROGRAM="${COMPILED_PROGRAM_PATH%.*}"
INPUT_TYPE="$2"
CRASH_DIR="$3"
NUM_CRASHES="$4"

# Counter for crashes analyzed
CRASH_COUNT=0

# Iterate through crash files in the directory
for CRASH_FILE in "$CRASH_DIR"/id:*; do
    # Stop if we have processed the desired number of crashes
    if [ "$CRASH_COUNT" -ge "$NUM_CRASHES" ]; then
        break
    fi

    # Run gdb to analyze the crash
    if [ "$INPUT_TYPE" == "@@" ]; then
        # Input from file
        gdb_output=$(gdb --batch --ex "file $COMPILED_PROGRAM" --ex "run $CRASH_FILE" --ex "backtrace" 2>/dev/null)
    elif [ "$INPUT_TYPE" == "@" ]; then
        # Input from stdin
        gdb_output=$(gdb --batch --ex "file $COMPILED_PROGRAM" --ex "run < $CRASH_FILE" --ex "backtrace" 2>/dev/null)
    fi

    # Extract the function causing the crash from the gdb output
    crash_function=$(echo "$gdb_output")

    # Output the results
    if [ -n "$crash_function" ]; then
        echo "$crash_function"
    else
        echo "None"
    fi

    CRASH_COUNT=$((CRASH_COUNT + 1))
done
