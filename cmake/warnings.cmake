# Compiler warnings configuration

# Core warnings that should be enabled immediately
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall                # Enable all common warnings
    -Wextra              # Enable extra warnings
  )

  # Add more strict warnings only for our own code
  function(target_add_strict_warnings target)
    target_compile_options(${target} PRIVATE
      -Werror              # Treat warnings as errors
      -Wpedantic           # Enforce strict ISO C++
      -Wnull-dereference   # Warn about null pointer dereference
      -Wformat=2           # Warn about printf format issues
      -Wimplicit-fallthrough # Warn about fallthrough in switch statements
      -Wunused             # Warn about unused variables/functions

      # Conversion warnings (more aggressive)
      -Wconversion
      -Wsign-conversion
      -Wdouble-promotion

      # Shadow warnings  
      -Wshadow

      # Important structural warnings
      -Wunreachable-code
      -Wself-assign
      -Woverloaded-virtual 

      # More advanced warnings to implement gradually
      -Wrange-loop-analysis # Range-based for loop issues
      -Wredundant-move     # Unnecessary move operations
      -Wundef              # Undefined macro use in #if
      -Wdeprecated         # Deprecated feature usage
    )
  endfunction()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Similar flags for GCC
  add_compile_options(
    -Wall 
    -Wextra 
    -Wpedantic 
    -Wunused
    -Wnull-dereference 
    -Wformat=2 
    -Wimplicit-fallthrough
    -Werror              # Treat warnings as errors
  )
endif()