#!/bin/zsh
# Script to fix incorrect imgui_include.hpp paths

# Find all files with the incorrect path and fix them
find /Users/ignaciorodriguez/src/rouen/src -type f -name "*.hpp" -o -name "*.cpp" | xargs grep -l "/imgui_include.hpp" | while read file; do
  echo "Fixing $file"
  # Get the relative path to helpers directory
  dir_path=$(dirname "$file")
  rel_path=$(python3 -c "import os.path; print(os.path.relpath('/Users/ignaciorodriguez/src/rouen/src/helpers', '$dir_path'))")
  
  # Replace the incorrect path with the correct relative path
  sed -i "" "s|/imgui_include.hpp|$rel_path/imgui_include.hpp|g" "$file"
done

echo "Done fixing imgui_include.hpp paths!"
