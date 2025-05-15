#!/bin/bash
# Script to find and fix problematic ImGui include paths
# This script will fix the following issues:
# 1. Multiple consecutive helpers paths (../../helpers../../helpers)
# 2. Paths with ellipsis (.../imgui_include.hpp)
# 3. Paths with too many levels (../helpers../helpers../helpers)

PROJECT_PATH="/Users/ignaciorodriguez/src/rouen"
SRC_PATH="$PROJECT_PATH/src"

echo "===== Fixing problematic ImGui include paths ====="

# Function to get the correct relative path from a source file to the helpers directory
get_correct_path() {
  local file="$1"
  local dir=$(dirname "$file")
  local rel_path=$(python3 -c "import os.path; print(os.path.relpath('$SRC_PATH/helpers', '$dir'))")
  echo "$rel_path"
}

# Fix paths with multiple consecutive helpers
find "$SRC_PATH" -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | 
while IFS= read -r -d '' file; do
  if grep -q "../../helpers../../helpers" "$file"; then
    echo "Fixing duplicate helpers path in: $file"
    rel_path=$(get_correct_path "$file")
    sed -i "" "s|../../helpers../../helpers/|$rel_path/|g" "$file"
  fi
done

# Fix paths with ellipsis
find "$SRC_PATH" -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | 
while IFS= read -r -d '' file; do
  if grep -q "\.\.\./" "$file"; then
    echo "Fixing ellipsis path in: $file"
    rel_path=$(get_correct_path "$file")
    sed -i "" "s|\.\.\./imgui_include\.hpp|$rel_path/imgui_include.hpp|g" "$file"
  fi
done

# Fix paths with multiple helpers levels
find "$SRC_PATH" -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | 
while IFS= read -r -d '' file; do
  if grep -q "../helpers\.\./helpers" "$file"; then
    echo "Fixing complex helpers path in: $file"
    rel_path=$(get_correct_path "$file")
    sed -i "" "s|../helpers\.\./helpers\.\./helpers/|$rel_path/|g" "$file"
    sed -i "" "s|../helpers\.\./helpers/|$rel_path/|g" "$file"
  fi
done

# Fix any absolute paths
find "$SRC_PATH" -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | 
while IFS= read -r -d '' file; do
  if grep -q "#include \"/imgui_include\.hpp\"" "$file"; then
    echo "Fixing absolute path in: $file"
    rel_path=$(get_correct_path "$file")
    sed -i "" "s|#include \"/imgui_include\.hpp\"|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
  fi
done

# Fix helpers path in the helpers directory itself
find "$SRC_PATH/helpers" -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | 
while IFS= read -r -d '' file; do
  # Skip imgui_include.hpp itself
  if [[ "$file" == *"imgui_include.hpp" ]]; then
    continue
  fi
  
  if grep -q "#include \"helpers/imgui_include\.hpp\"" "$file"; then
    echo "Fixing incorrect helpers path in helpers dir: $file"
    sed -i "" "s|#include \"helpers/imgui_include\.hpp\"|#include \"./imgui_include.hpp\"|g" "$file"
  fi
done

echo "===== Path fixing complete ====="
