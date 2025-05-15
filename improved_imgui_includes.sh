#!/bin/bash
# Comprehensive script to update all ImGui includes in the project
# Usage: ./improved_imgui_includes.sh

# Base project path - adjust as needed
PROJECT_PATH="/Users/ignaciorodriguez/src/rouen"
SRC_PATH="$PROJECT_PATH/src"
HELPERS_PATH="$SRC_PATH/helpers"

echo "===== Improving ImGui includes ====="
echo "Project path: $PROJECT_PATH"
echo "Source path: $SRC_PATH"
echo "Helpers path: $HELPERS_PATH"

# Function to compute relative path from source file to helpers directory
get_relative_path() {
  local file_dir="$1"
  python3 -c "import os.path; print(os.path.relpath('$HELPERS_PATH', '$file_dir'))"
}

# Find files with ImGui includes (excluding the wrapper itself)
find_imgui_includes() {
  local pattern="$1"
  find "$SRC_PATH" -type f \( -name "*.hpp" -o -name "*.cpp" \) | xargs grep -l "$pattern" | grep -v "imgui_include.hpp"
}

# Fix duplicated paths (like ../../helpers../../helpers)
fix_duplicated_paths() {
  echo "Fixing duplicated helper paths..."
  
  # Find files with duplicated paths
  grep -r --include="*.hpp" --include="*.cpp" "../../helpers../../helpers" "$SRC_PATH" | cut -d':' -f1 | uniq | while read file; do
    echo "  Fixing duplicated path in $file"
    dir_path=$(dirname "$file")
    rel_path=$(get_relative_path "$dir_path")
    # Replace duplicated paths with correct relative path
    sed -i "" "s|../../helpers../../helpers/|$rel_path/|g" "$file"
  done
}

# Fix absolute paths (/imgui_include.hpp)
fix_absolute_paths() {
  echo "Fixing absolute imgui paths..."
  
  # Find files with absolute paths to the wrapper
  grep -r --include="*.hpp" --include="*.cpp" "/imgui_include.hpp" "$SRC_PATH" | cut -d':' -f1 | uniq | while read file; do
    echo "  Fixing absolute path in $file"
    dir_path=$(dirname "$file")
    rel_path=$(get_relative_path "$dir_path")
    # Replace absolute paths with correct relative path
    sed -i "" "s|/imgui_include.hpp|$rel_path/imgui_include.hpp|g" "$file"
  done
}

# Replace all direct ImGui includes with our wrapper
replace_imgui_includes() {
  echo "Replacing direct ImGui includes..."
  
  # First handle angle bracket includes: #include <imgui.h>
  find_imgui_includes "#include <imgui.h>" | while read file; do
    echo "  Updating $file"
    dir_path=$(dirname "$file")
    rel_path=$(get_relative_path "$dir_path")
    sed -i "" "s|#include <imgui.h>|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
  done
  
  # Then handle quoted includes: #include "imgui.h"
  find_imgui_includes "#include \"imgui.h\"" | while read file; do
    echo "  Updating $file"
    dir_path=$(dirname "$file")
    rel_path=$(get_relative_path "$dir_path")
    sed -i "" "s|#include \"imgui.h\"|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
  done
  
  # Handle imgui_internal.h as well
  find_imgui_includes "#include <imgui_internal.h>" | while read file; do
    echo "  Updating $file (imgui_internal.h)"
    dir_path=$(dirname "$file")
    rel_path=$(get_relative_path "$dir_path")
    sed -i "" "s|#include <imgui_internal.h>|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
  done
  
  find_imgui_includes "#include \"imgui_internal.h\"" | while read file; do
    echo "  Updating $file (imgui_internal.h)"
    dir_path=$(dirname "$file")
    rel_path=$(get_relative_path "$dir_path")
    sed -i "" "s|#include \"imgui_internal.h\"|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
  done
  
  # Also handle backend includes if necessary
  for backend in "imgui_impl_sdl2.h" "imgui_impl_opengl3.h" "imgui_impl_sdlrenderer2.h"
  do
    find_imgui_includes "#include <$backend>" | while read file; do
      echo "  Updating $file ($backend)"
      dir_path=$(dirname "$file")
      rel_path=$(get_relative_path "$dir_path")
      sed -i "" "s|#include <$backend>|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
    done
    
    find_imgui_includes "#include \"$backend\"" | while read file; do
      echo "  Updating $file ($backend)"
      dir_path=$(dirname "$file")
      rel_path=$(get_relative_path "$dir_path")
      sed -i "" "s|#include \"$backend\"|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
    done
  done
}

# Verify all ImGui includes have been handled
verify_includes() {
  echo "Verifying includes..."
  
  # Check for remaining direct ImGui includes
  remaining_includes=$(find_imgui_includes "#include [<\"]imgui")
  if [ -n "$remaining_includes" ]; then
    echo "WARNING: Some ImGui includes were not replaced:"
    echo "$remaining_includes"
    return 1
  else
    echo "All ImGui includes have been properly replaced."
    return 0
  fi
}

# Main execution
echo "Starting ImGui inclusion fixes..."
fix_duplicated_paths
fix_absolute_paths
replace_imgui_includes
verify_includes

echo "Done!"
