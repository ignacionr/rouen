# Compiler warnings configuration

# Add option to enable CI-level strictness for local development
option(ENABLE_CI_STRICTNESS "Enable CI-level warning strictness for local development" OFF)

# Core warnings that should be enabled immediately
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall                # Enable all common warnings
    -Wextra              # Enable extra warnings
    -Wshadow             # Warn about shadowed declarations
    -Wunused             # Enable unused warnings category
    -Wunused-result      # Warn about unused return values
    -Wmissing-field-initializers # Warn about missing field initializers
    -Wpedantic           # Strict ISO C++ compliance
    -Wconversion         # Warn on implicit conversions
    -Wsign-conversion    # Warn on sign conversions
    -Wnull-dereference   # Warn on null dereferences
    -Wformat=2           # String format checks
    -Wimplicit-fallthrough # Switch fallthrough checks
    -Wunreachable-code   # Unreachable code detection
    -Wcast-align         # Pointer cast alignment checks
    -Wcast-qual          # Qualifier cast checks
    -Wzero-as-null-pointer-constant # Modern null pointer checks
  )

  # Add more strict warnings only for our own code
  function(target_add_strict_warnings target)
    set(WARN_ERR_OPTION -Werror)
    if(DEFINED CMAKE_COMPILE_WARNING_AS_ERROR AND NOT CMAKE_COMPILE_WARNING_AS_ERROR)
      set(WARN_ERR_OPTION "")
    endif()
    target_compile_options(${target} PRIVATE
      ${WARN_ERR_OPTION}
      -Wno-error=old-style-cast
      -Wno-error=cast-function-type-strict
      -Wno-error=cast-function-type
      -Wno-old-style-cast
      -Wno-cast-function-type-strict
      -Wno-decls-in-multiple-modules
      -Wno-error=decls-in-multiple-modules
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
      -Wimplicit-int-conversion
      -Wimplicit-float-conversion
      -Wfloat-conversion
      -Wshorten-64-to-32
      -Wnarrowing
      -Wsign-compare

      # Shadow warnings  
      -Wshadow
      -Wshadow-all         # Even more aggressive shadowing detection
      -Wshadow-field
      -Wshadow-uncaptured-local

      # Lifetime & Dangling reference warnings
      -Wdangling
      -Wdangling-field
      -Wdangling-initializer-list
      -Wdangling-gsl
      -Wdangling-else
      -Wreturn-stack-address

      # Logic, Comparison & Tautological warnings
      -Wtautological-compare
      -Wtautological-type-limit-compare
      -Wtautological-overlap-compare
      -Wtautological-unsigned-enum-zero-compare
      -Wtautological-value-range-compare
      -Wtautological-pointer-compare
      -Wtautological-negation-compare
      -Wfloat-equal
      -Wcomma
      -Wswitch-enum

      # Type Cast & Pointer warnings
      -Wcast-align
      -Wcast-qual
      -Wzero-as-null-pointer-constant
      -Wstring-conversion
      -Wstring-concatenation
      -Wpointer-arith
      -Wnull-pointer-subtraction

      # Important structural and control flow warnings
      -Wunreachable-code
      -Wunreachable-code-break
      -Wunreachable-code-return
      -Wunreachable-code-loop-increment
      -Wunreachable-code-aggressive
      -Wself-assign
      -Wself-assign-overloaded
      -Wself-assign-field
      -Wself-move
      -Woverloaded-virtual 

      # More advanced warnings to catch issues
      -Wrange-loop-analysis # Range-based for loop issues
      -Wrange-loop-bind-reference
      -Wredundant-move     # Unnecessary move operations
      -Wpessimizing-move
      -Wundef              # Undefined macro use in #if
      -Wdeprecated         # Deprecated feature usage
      -Wdeprecated-copy
      -Wdeprecated-copy-dtor
      -Wdeprecated-copy-with-user-declared-copy
      -Wdeprecated-copy-with-user-declared-dtor
      -Wmissing-field-initializers   # Warn about missing field initializers
      
      # Additional strict warnings available in Clang
      -Wdangling-else            # Ambiguous dangling else
      -Wempty-body               # Empty bodies in if/else/for
      -Wparentheses              # Missing parentheses
      -Widiomatic-parentheses
      -Wlogical-op-parentheses
      -Wlogical-not-parentheses
      -Wreturn-type              # Missing return statements
      -Wuninitialized            # Uninitialized variables
      -Wconditional-uninitialized # Clang-specific uninitialized variable detection
      
      # Memory and pointer warnings (Clang-specific)
      -Warray-bounds             # Array bounds checking
      -Warray-bounds-pointer-arithmetic # Pointer arithmetic bounds
      
      # C++ specific strict warnings & Overrides
      -Wnon-virtual-dtor         # Missing virtual destructors
      -Winconsistent-missing-override # Missing override keywords
      -Winconsistent-missing-destructor-override
      -Wsuggest-override
      -Wsuggest-destructor-override
      -Wabstract-final-class
      -Wold-style-cast           # C-style casts in C++
      -Wextra-semi               # Extra semicolons
      -Wloop-analysis            # Loop analysis warnings
      -Wmove                     # Move semantic issues
      -Wthread-safety            # Thread safety analysis
      -Wthread-safety-analysis   # More thread safety checks
      -Wthread-safety-precise
      -Wthread-safety-attributes
      -Wthread-safety-beta
      -Wctor-dtor-privacy
      -Wsign-promo
      -Wstrict-prototypes

      # Bitwise, Shift & Enum warnings
      -Wshift-overflow
      -Wshift-sign-overflow
      -Wbitwise-op-parentheses
      -Wbitwise-conditional-parentheses
      -Wduplicate-enum
      -Wduplicate-decl-specifier
      -Wenum-conversion
      -Wenum-compare
      -Wenum-compare-conditional
      -Wenum-enum-conversion
      -Wenum-float-conversion
      -Wassign-enum
      
      # String and format warnings
      -Wformat-security          # Format string security
      -Wformat-nonliteral        # Non-literal format strings
      -Wformat-pedantic          # Pedantic format checking
      -Wformat-type-confusion    # Format type mismatches

      # Documentation warnings
      -Wdocumentation
      -Wdocumentation-unknown-command
      -Wdocumentation-pedantic

      # Unused entity warnings
      -Wunused-exception-parameter
      -Wunused-template
      -Wunused-lambda-capture
      -Wunused-private-field
      -Wunused-member-function
      -Wunused-const-variable
      -Wunused-local-typedef
      -Wunused-macros
      -Wunneeded-internal-declaration
      -Wused-but-marked-unused

      # Misc strictness
      -Wdate-time
      -Watomic-alignment
      -Wextra-qualification
      -Wnewline-eof
      -Wvla
      -Wmissing-noreturn
      -Wuser-defined-warnings
      -Wctad-maybe-unsupported
      -Wpoison-system-directories
      -Wconsumed
      -Wunnamed-type-template-args
      -Wzero-length-array
      -Wfinal-macro
      -Wsubobject-linkage
      -Wvector-conversion
      -Wambiguous-reversed-operator

      # Header and Macro Hygiene
      -Wundefined-func-template  # Undefined function templates
      -Wundefined-inline
      -Wundefined-reinterpret-cast
      -Wheader-hygiene           # Main header hygiene checks
      
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
      -Wno-extra-semi-stmt       # Macro expansions ending with semicolon
      -Wno-unknown-warning-option # Ignore unknown warning flags across clang versions
    )
  endfunction()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Similar flags for GCC
  add_compile_options(
    -Wall 
    -Wextra 
    -Wno-pedantic 
    -Wshadow
    -Wunused
    -Wunused-result      # Warn about unused return values
    -Wmissing-field-initializers
    -Wnull-dereference 
    -Wformat=2 
    -Wimplicit-fallthrough
  )
  
  # Add function for GCC too
  function(target_add_strict_warnings target)
    set(WARN_ERR_OPTION -Werror)
    if(DEFINED CMAKE_COMPILE_WARNING_AS_ERROR AND NOT CMAKE_COMPILE_WARNING_AS_ERROR)
      set(WARN_ERR_OPTION "")
    endif()
    target_compile_options(${target} PRIVATE
      ${WARN_ERR_OPTION}

      # GCC 14+ emits false-positive -Wnull-dereference warnings in <streambuf>
      # when istreambuf_iterator is used (inlined from standard library headers).
      # This is a known GCC 14 regression; disable the warning to avoid build log spam.
      $<$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},14.0>:-Wno-null-dereference>
      $<$<VERSION_GREATER_EQUAL:${CMAKE_CXX_COMPILER_VERSION},15.0>:-Wno-template-names-tu-local>
      -Wno-subobject-linkage
      -Wno-pedantic

      # CI Strictness: Make unused-result specifically an error in all builds
      $<$<BOOL:${ENABLE_CI_STRICTNESS}>:-Werror=unused-result>
      
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wunused
      -Wunused-result
      -Wunreachable-code
      -Wmissing-field-initializers
    )
  endfunction()

elseif(MSVC)
  # MSVC-specific warnings configuration
  add_compile_options(
    /W4          # High warning level
    /permissive- # Disable non-conforming code
    /w14456      # Warn on declaration hiding local variable (-Wshadow equivalent)
    /w14457      # Warn on declaration hiding function parameter (-Wshadow equivalent)
    /w14458      # Warn on declaration hiding class member (-Wshadow equivalent)
    /w14459      # Warn on declaration hiding global declaration (-Wshadow equivalent)
    /w14834      # Warn on discarding return value of function with [[nodiscard]] attribute (-Wunused-result equivalent)
  )
  
  # Function for adding strict warnings to MSVC targets
  function(target_add_strict_warnings target)
    target_compile_options(${target} PRIVATE
      /WX          # Treat warnings as errors
      /w14456      # Warn on declaration hiding local variable
      /w14457      # Warn on declaration hiding function parameter
      /w14458      # Warn on declaration hiding class member
      /w14459      # Warn on declaration hiding global declaration
      /w14834      # Warn on discarding return value of function with [[nodiscard]] attribute
      /wd4267      # Suppress 'conversion from size_t to int' warnings
      /wd4244      # Suppress 'conversion from double to float' warnings
      /wd4101      # Suppress 'unreferenced local variable' warnings
      /wd4996      # Suppress deprecated function warnings
    )
  endfunction()
endif()