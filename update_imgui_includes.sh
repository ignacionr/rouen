#!/bin/zsh
# Improved script to update all ImGui includes in the project

# Find all files that include imgui.h directly
find /Users/ignaciorodriguez/src/rouen/src -type f -name "*.hpp" -o -name "*.cpp" | xargs grep -l "#include <imgui.h>" | while read file; do
  # Skip our wrapper file
  if [[ "$file" == */imgui_include.hpp ]]; then
    continue
  fi
  
  echo "Updating $file"
  # Get the relative path to helpers directory
  dir_path=$(dirname "$file")
  rel_path=$(python3 -c "import os.path; print(os.path.relpath('/Users/ignaciorodriguez/src/rouen/src/helpers', '$dir_path'))")
  
  # Replace the direct include with our wrapper
  sed -i "" "s|#include <imgui.h>|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
done

# Also replace relative paths that might exist
find /Users/ignaciorodriguez/src/rouen/src -type f -name "*.hpp" -o -name "*.cpp" | xargs grep -l "#include \"imgui.h\"" | while read file; do
  # Skip our wrapper file
  if [[ "$file" == */imgui_include.hpp ]]; then
    continue
  fi
  
  echo "Updating $file (relative path)"
  # Get the relative path to helpers directory
  dir_path=$(dirname "$file")
  rel_path=$(python3 -c "import os.path; print(os.path.relpath('/Users/ignaciorodriguez/src/rouen/src/helpers', '$dir_path'))")
  
  # Replace the direct include with our wrapper
  sed -i "" "s|#include \"imgui.h\"|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
done

echo "Done! All ImGui includes have been updated to use the wrapper."
