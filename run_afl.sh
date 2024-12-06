#!/bin/bash

# Check if a file path is provided
if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <file_path> <fuzz_time> <input_type> <sudo_password>"
  exit 1
fi

file_path="$1"
file_name="${file_path%.*}"
fuzz_time=$2
input_type=$3
sudo_password=$4

# Set up core pattern for crash dumps
echo "$sudo_password" | sudo -S sh -c 'echo "core" > /proc/sys/kernel/core_pattern'

# Compile the file with afl-gcc and address sanitizer
afl-gcc -g -w "$file_path" -o "$file_name" 2> compilation_log.txt
afl-g++ -g -w "$file_path" -o "$file_name" 2>> compilation_log.txt

afl-fuzz -i input -o output -m none -- ./"$file_name" "$input_type" &
afl_pid=$!

sleep 1

if ! kill -0 "$afl_pid" 2>/dev/null; then
    exit 2
fi

sleep "$fuzz_time"  # Let AFL run for fuzz_time seconds
kill $afl_pid
sleep 1
kill -SIGINT $$
exit 0