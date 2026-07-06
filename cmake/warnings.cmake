# Compiler warnings configuration

# Add option to enable CI-level strictness for local development
option(ENABLE_CI_STRICTNESS "Enable CI-level warning strictness for local development" OFF)

# Core warnings that should be enabled immediately
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall                # Enable all common warnings
    -Wextra              # Enable extra warnings
  )

  # Add more strict warnings only for our own code
  function(target_add_strict_warnings target)
    target_compile_options(${target} PRIVATE
      -Werror              # Treat warnings as errors - ensure local code has no warnings
      -Wpedantic           # Enforce strict ISO C++
      -Wnull-dereference   # Warn about null pointer dereference
      -Wformat=2           # Warn about printf format issues
      -Wimplicit-fallthrough # Warn about fallthrough in switch statements
      -Wunused             # Warn about unused variables/functions
      -Wunused-result      # Warn about unused return values (catches system() calls)

      # CI Strictness: Make unused-result specifically an error in all builds
      $<$<BOOL:${ENABLE_CI_STRICTNESS}>:-Werror=unused-result>

      # Conversion warnings (more aggressive)
      -Wconversion
      -Wsign-conversion
      -Wdouble-promotion

      # Shadow warnings  
      -Wshadow
      -Wshadow-all         # Even more aggressive shadowing detection

      # Important structural warnings
      -Wunreachable-code
      -Wself-assign
      -Woverloaded-virtual 

      # More advanced warnings to catch issues
      -Wrange-loop-analysis # Range-based for loop issues
      -Wredundant-move     # Unnecessary move operations
      -Wundef              # Undefined macro use in #if
      -Wdeprecated         # Deprecated feature usage
      -Wno-missing-field-initializers # Allow designated initializers (C++23 feature)
      
      # Additional strict warnings available in Clang
      -Wdangling-else            # Ambiguous dangling else
      -Wempty-body               # Empty bodies in if/else/for
      -Wparentheses              # Missing parentheses
      -Wreturn-type              # Missing return statements
      -Wuninitialized            # Uninitialized variables
      -Wconditional-uninitialized # Clang-specific uninitialized variable detection
      
      # Memory and pointer warnings (Clang-specific)
      -Warray-bounds             # Array bounds checking
      -Warray-bounds-pointer-arithmetic # Pointer arithmetic bounds
      
      # C++ specific strict warnings
      -Wnon-virtual-dtor         # Missing virtual destructors
      -Wold-style-cast           # C-style casts in C++
      -Wextra-semi               # Extra semicolons
      -Wno-extra-semi-stmt       # Temporarily disable for macro issues
      -Winconsistent-missing-override # Missing override keywords
      -Wloop-analysis            # Loop analysis warnings
      -Wmove                     # Move semantic issues
      -Wthread-safety            # Thread safety analysis
      -Wthread-safety-analysis   # More thread safety checks
      
      # String and format warnings
      -Wformat-pedantic          # Pedantic format checking
      -Wformat-type-confusion    # Format type mismatches
      
      # Enable everything and then disable specific ones we don't want
      -Weverything
      # Disable warnings we don't want from -Weverything
      -Wno-c++98-compat          # We use modern C++
      -Wno-c++98-compat-pedantic
      -Wno-padded                # Struct padding is usually fine
      -Wno-weak-vtables          # Header-only classes are ok
      -Wno-exit-time-destructors # Static destructors are sometimes needed
      -Wno-global-constructors   # Global constructors sometimes needed
      -Wno-missing-prototypes    # C++ doesn't need this
      -Wno-missing-variable-declarations # C++ doesn't need this
      -Wno-missing-include-dirs  # Temporarily disable to debug include issues
      -Wno-disabled-macro-expansion # curl and other system macros
      -Wno-covered-switch-default   # Conflicts with -Wswitch-default for exhaustive enums
      -Wno-switch-default           # Allow exhaustive enum switches without default
      -Wno-unsafe-buffer-usage      # Legacy C-style buffer usage (would require major refactoring)
      -Wno-reserved-macro-identifier # Allow mongoose's internal macros
      -Wno-nrvo                  # Allow return value copy elision warnings
      -Wno-thread-safety-negative # Disable negative capability thread warnings
      -Wno-unknown-warning-option # Ignore unknown warning flags across clang versions
    )
  endfunction()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Similar flags for GCC
  add_compile_options(
    -Wall 
    -Wextra 
    -Wpedantic 
    -Wunused
    -Wunused-result      # Warn about unused return values
    -Wnull-dereference 
    -Wformat=2 
    -Wimplicit-fallthrough
  )
  
  # Add function for GCC too
  function(target_add_strict_warnings target)
    target_compile_options(${target} PRIVATE
      -Werror              # Treat warnings as errors

      # GCC 14+ emits false-positive -Wnull-dereference warnings in <streambuf>
      # when istreambuf_iterator is used (inlined from standard library headers).
      # This is a known GCC 14 regression; disable the warning to avoid build log spam.
      $<$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},14.0>:-Wno-null-dereference>

      # CI Strictness: Make unused-result specifically an error in all builds
      $<$<BOOL:${ENABLE_CI_STRICTNESS}>:-Werror=unused-result>
      
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wunreachable-code
    )
  endfunction()

elseif(MSVC)
  # MSVC-specific warnings configuration
  add_compile_options(
    /W4          # High warning level
    /permissive- # Disable non-conforming code
  )
  
  # Function for adding strict warnings to MSVC targets
  function(target_add_strict_warnings target)
    target_compile_options(${target} PRIVATE
      /WX          # Treat warnings as errors
      /wd4267      # Suppress 'conversion from size_t to int' warnings
      /wd4244      # Suppress 'conversion from double to float' warnings
      /wd4101      # Suppress 'unreferenced local variable' warnings
      /wd4996      # Suppress deprecated function warnings
    )
  endfunction()
endif()