#!/bin/bash
# filepath: scripts/count_lines.sh

echo "Counting lines in source files..."

# Find all source files, excluding external dependencies and build directories
# Focusing on common C++ extensions and other relevant files
find . -type f \
    -not -path "*/\.*" \
    -not -path "*/build/*" \
    -not -path "*/external/*" \
    -not -path "*/third_party/*" \
    -not -path "*/deps/*" \
    -not -path "*/node_modules/*" \
    \( -name "*.cpp" -o -name "*.hpp" -o -name "*.cc" -o -name "*.h" -o -name "*.c" -o -name "*.mm" -o -name "*.m" -o -name "*.swift" \) \
    | xargs wc -l 2>/dev/null | sort -nr | head -n 20