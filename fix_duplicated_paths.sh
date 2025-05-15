#!/bin/zsh
# Script to fix duplicated helpers paths

# Find all files with duplicated ../../helpers../../helpers/ paths
find /Users/ignaciorodriguez/src/rouen/src -type f -name "*.hpp" -o -name "*.cpp" | xargs grep -l "../../helpers../../helpers/" | while read file; do
  echo "Fixing duplicated path in $file"
  # Replace the duplicated path with the correct one
  sed -i "" "s|../../helpers../../helpers/|../../helpers/|g" "$file"
done

echo "Done fixing duplicated paths!"
