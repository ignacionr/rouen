---
name: cpp20-module-migration
description: Best practices, CMake configuration rules, and step-by-step workflow for converting C++ hybrid header wrappers into pure native C++20 module interface and implementation units (.cppm/.cpp).
---

# C++20 Native Module Migration Skill

Use this skill when refactoring hybrid C++ header wrapper files (`.hpp`/`.cpp`) into pure native C++20 modules (`.cppm`/`.cpp`) using CMake 3.28+ and Ninja.

---

## Core Structure of a Native C++20 Module

A native C++20 module split consists of three parts:

### 1. Primary Module Interface Unit (`.cppm`)
* **Role**: Declares the module interface and exports exported types, namespaces, and functions.
* **Rule**: All `#include` directives must reside inside the **Global Module Fragment** (`module;`) before `export module`.

```cpp
module;

// Global Module Fragment (Headers needed for type definitions)
#include <chrono>
#include <string>

export module rouen.models.rss.rss_date_parser;

export namespace media::rss {
    std::chrono::system_clock::time_point parse_rss_date(const char* date_str);
    std::string format_rss_age(std::chrono::system_clock::time_point const& publish_date);
}
```

---

### 2. Module Implementation Unit (`.cpp`)
* **Role**: Contains member and function definitions.
* **Rule**: Uses `module <module_name>;` to attach implementation to the named module interface unit.

```cpp
module;

// Global Module Fragment
#include <chrono>
#include <ctime>
#include <iomanip>
#include <format>
#include <regex>
#include <string>

module rouen.models.rss.rss_date_parser;

namespace media::rss {
    std::chrono::system_clock::time_point parse_rss_date(const char* date_str) {
        // Implementation
    }
}
```

---

### 3. Legacy Header Bridge (`.hpp`)
* **Role**: Backward compatibility for legacy non-module translation units.
* **Rule**: Forwards directly to the native module import.

```cpp
#pragma once

import rouen.models.rss.rss_date_parser;
```

---

## Critical CMake 3.28+ / Ninja Rules

### 1. Register Module Interfaces in `FILE_SET CXX_MODULES`
CMake must be explicitly told which `.cppm` files are C++20 module interface units so Ninja performs `dyndep` scanning (`CXX.dd`) and compiles Binary Module Interfaces (`.pcm`/`.bmi`) before dependent files build.

In `CMakeLists.txt`:

```cmake
target_sources(${PROJECT_NAME}
  PUBLIC
  FILE_SET CXX_MODULES FILES
    src/hosts/quickjs_host.cppm
    src/models/rss/rss_date_parser.cppm   # Must be registered here!
)
```

> **Warning**: Omitting a `.cppm` file from `FILE_SET CXX_MODULES FILES` causes compilation failure with `fatal error: module '<name>' not found`.

---

## Step-by-Step Migration Checklist

1. **Create/Update `.cppm` Interface**:
   - Place `#include` directives in `module;` block.
   - Define `export module <module_name>;`.
   - Wrap exported symbols in `export namespace <ns> { ... }`.
2. **Update `.cpp` Implementation**:
   - Place `#include` directives in `module;` block.
   - Declare `module <module_name>;`.
3. **Update `.hpp` Compatibility Bridge**:
   - Forward with `import <module_name>;`.
4. **Update `CMakeLists.txt`**:
   - Add `.cppm` file to `FILE_SET CXX_MODULES FILES`.
5. **Re-configure & Validate**:
   - Run `cmake -G Ninja -B build -S .` to generate dyndep rules.
   - Run `cmake --build build --target <target>` to compile and link.
