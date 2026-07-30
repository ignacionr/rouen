#!/usr/bin/env bash
set -euo pipefail

# Script to run clang-tidy on Rouen project source files with proper Nix header resolution.
# Usage: ./scripts/run_clang_tidy.sh [CHECK_PATTERN] [--fix]
# Example: ./scripts/run_clang_tidy.sh misc-include-cleaner
# Example: ./scripts/run_clang_tidy.sh misc-const-correctness --fix

CHECK_PATTERN="${1:-misc-const-correctness}"
FIX_FLAG=""

if [[ "${2:-}" == "--fix" ]] || [[ "${1:-}" == "--fix" ]]; then
  FIX_FLAG="-fix"
fi

if [[ "${1:-}" == "--fix" ]]; then
  CHECK_PATTERN="misc-const-correctness"
fi

# Locate Nix environment include directories
CLANG_INC="$(find /nix/store -maxdepth 5 -path "*/clang/20/include" -type d 2>/dev/null | head -n 1)"
LIBCXX_INC="$(find /nix/store -maxdepth 5 -path "*/include/c++/v1" -type d 2>/dev/null | head -n 1)"
SDK_INC="$(find /nix/store -maxdepth 6 -path "*/Developer/SDKs/MacOSX.sdk/usr/include" -type d 2>/dev/null | head -n 1)"

extra=()
if [ -n "$LIBCXX_INC" ]; then
  extra+=("-extra-arg-before=-isystem$LIBCXX_INC")
fi
if [ -n "$CLANG_INC" ]; then
  extra+=("-extra-arg-before=-isystem$CLANG_INC")
fi
if [ -n "$SDK_INC" ]; then
  extra+=("-extra-arg-before=-isystem$SDK_INC")
fi

prev=""
for token in ${NIX_CFLAGS_COMPILE:-}; do
  if [ "$prev" = "-isystem" ]; then
    extra+=("-extra-arg-before=-isystem$token")
    prev=""
  elif [ "$token" = "-isystem" ]; then
    prev="-isystem"
  else
    extra+=("-extra-arg-before=$token")
  fi
done

# Ensure compile_commands.json is up-to-date with PCH disabled for accurate AST parsing
cmake -G Ninja -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DDISABLE_PCH=ON >/dev/null

echo "Running clang-tidy with checks: -*,$CHECK_PATTERN"
run-clang-tidy $FIX_FLAG -p build -checks="-*,$CHECK_PATTERN" -j2 "${extra[@]}" ".*/rouen/src/.*"

if [ -n "$FIX_FLAG" ]; then
  echo "Cleaning up non-portable internal libc++ header inclusions (#include <__...>)..."
  find src/ -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.mm" \) -exec sed -i '' '/#include <__/d' {} +
fi
