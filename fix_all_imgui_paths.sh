#!/bin/bash
# More comprehensive script to fix all ImGui include path problems

PROJECT_PATH="/Users/ignaciorodriguez/src/rouen"
SRC_PATH="$PROJECT_PATH/src"

echo "===== Comprehensive ImGui include path fix ====="

fix_includes() {
  local file="$1"
  local dir=$(dirname "$file")
  local rel_path=$(python3 -c "import os.path; print(os.path.relpath('$SRC_PATH/helpers', '$dir'))")
  
  # Skip the wrapper file itself
  if [[ "$file" == *"imgui_include.hpp" ]]; then
    return
  fi
  
  # Check for any kind of complex or problematic path
  if grep -q "helpers.*helpers.*imgui_include\.hpp" "$file" || 
     grep -q "\.\.\./.*imgui_include\.hpp" "$file" || 
     grep -q "helpers.*helpers" "$file" || 
     grep -q "/imgui_include\.hpp" "$file" || 
     grep -q "helpershelpers" "$file"; then
    
    echo "Fixing problematic path in: $file"
    
    # Get the current content
    local content=$(cat "$file")
    
    # Replace all problematic include patterns with the correct path
    # This is a more direct approach that just rewrites any problematic include
    # with the correct path, regardless of how broken it might be
    content=$(echo "$content" | perl -pe "s|#include [\"<][\.\/]*helpers[\.\/]*helpers[\.\/]*helpers[\.\/]*helpers[\/\.]*/imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g")
    content=$(echo "$content" | perl -pe "s|#include [\"<][\.\/]*helpers[\.\/]*helpers[\.\/]*helpers[\/\.]*/imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g")
    content=$(echo "$content" | perl -pe "s|#include [\"<][\.\/]*helpers[\.\/]*helpers[\/\.]*/imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g")
    content=$(echo "$content" | perl -pe "s|#include [\"<]helpershelpers.*/imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g")
    content=$(echo "$content" | perl -pe "s|#include [\"<][\.]{3}/imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g")
    content=$(echo "$content" | perl -pe "s|#include [\"<]/imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g")
    
    # Write back to the file
    echo "$content" > "$file"
  fi
}

# Special case for files in the helpers directory itself
fix_helper_files() {
  local file="$1"
  
  # Skip the wrapper file itself
  if [[ "$file" == *"imgui_include.hpp" ]]; then
    return
  fi
  
  # For files directly in helpers, use ./imgui_include.hpp
  if [[ "$file" == "$SRC_PATH/helpers/"*.hpp ]]; then
    echo "Fixing path in helpers file: $file"
    sed -i "" 's|#include [\"<].*imgui_include\.hpp[\">]|#include "./imgui_include.hpp"|g' "$file"
  fi
  
  # For files in subdirectories of helpers
  local subdir=$(dirname "$file" | sed "s|$SRC_PATH/helpers/||")
  if [[ -n "$subdir" && "$subdir" != "." ]]; then
    echo "Fixing path in helpers subdir file: $file"
    local rel_path=$(python3 -c "import os.path; print(os.path.relpath('$SRC_PATH/helpers', '$(dirname "$file")'))")
    sed -i "" "s|#include [\"<].*imgui_include\.hpp[\">]|#include \"$rel_path/imgui_include.hpp\"|g" "$file"
  fi
}

# Process all header and source files
find "$SRC_PATH" -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | 
while IFS= read -r -d '' file; do
  if [[ "$file" == *"/helpers/"* ]]; then
    fix_helper_files "$file"
  else
    fix_includes "$file"
  fi
done

echo "===== Path fixing complete ====="
