# Development Guide

Rouen's development environment is fully reproducible and isolated using **Nix**. This page details how to compile, debug, and run tests locally.

---

## Development Environment with Nix

Using Nix guarantees that all libraries, dependencies, toolchains (including modern compilers and CMake), and target SDKs are pinned and identical to the ones used in the CI/CD pipeline.

### Getting Started

1. **Install Nix**:
   Follow the [Nix Installation Guide](https://nixos.org/download.html).
2. **Enter the Nix Shell**:
   Run the following in the repository root:
   ```bash
   nix develop
   # or legacy shell
   nix-shell
   ```
   This loads GCC 14, Clang 19, SDL2, OpenSSL, curl, SQLite3, Glaze, ImGui, and pins system path environments.

3. **Build the Application**:
   ```bash
   # Configure with Nix toolchain
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake
   
   # Build
   cmake --build build --parallel
   
   # Run
   ./build/rouen.app/Contents/MacOS/rouen  # macOS
   ./build/rouen                           # Linux
   ```

### Precompiled Headers (PCH)
To speed up builds by 20-50%, Rouen uses a dual-tier precompiled header strategy. It is enabled by default. If you need to disable PCH (e.g. for specific static analysis tools or debugging compiler behavior), run:
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DDISABLE_PCH=ON
```

---

## Running Unit Tests

Rouen uses Google Test (`gtest`) for modern unit testing and hosts legacy test scripts.

### Build and Run Tests
Run the following inside the Nix development shell:
```bash
# Configure tests
cmake -B build-tests -S tests -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug

# Build tests
cmake --build build-tests --parallel

# Execute all tests
ctest --test-dir build-tests --output-on-failure
```

### VS Code Integration
The `.vscode/` directory contains pre-configured tasks and launch targets:
- **Build Task**: `Nix Build (Debug)` (accessible via `Cmd+Shift+B` or `Ctrl+Shift+B`).
- **Test Tasks**:
  - `Configure Tests (Nix)`
  - `Build Tests (Nix)`
  - `Run All Tests (Nix)`
- **Debugger Launches**: Pre-configured configurations exist under VS Code's Run panel to attach to tests and targets with complete debug symbols.

---

## Code Quality & CI/CD Parity

### Strict Warnings as Errors
All Rouen targets enforce strict compiler diagnostics. If a warning is emitted, the build fails.
- Configured in [warnings.cmake](file:///Users/ignaciorodriguez/src/rouen/cmake/warnings.cmake).
- Enables `-Werror`, `-Weverything` (under Clang), `-Wshadow-all`, `-Wconversion`, `-Wold-style-cast`, `-Wpedantic`, etc.
- Incompatibilities across compiler versions are automatically bypassed via `-Wno-unknown-warning-option`.

### CI Workflow (`ci-nix.yml`)
Every commit pushed or pull request opened triggers GitHub Actions:
- Uses the same Nix Flake environment as local development to compile and execute the complete test suite.

---

## Contributing Guidelines

### Creating a New Card

1. **Implement the Card Class** under `src/cards/`:
   ```cpp
   #include "card.hpp"
   
   namespace rouen::cards {
       class hello_card : public card {
       public:
           hello_card() {
               colors[0] = {0.1f, 0.4f, 0.7f, 1.0f}; // Primary theme color
               name("Hello Card");
               width = 350.0f;
           }
           
           bool render() override {
               return render_window([this]() {
                   ImGui::Text("Hello, World!");
               });
           }
           
           std::string get_uri() const override {
               return "hello-card";
           }
       };
   }
   ```
2. **Register the Card** inside [factory.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/factory.hpp):
   ```cpp
   {"hello-card", [](std::string_view uri, SDL_Renderer*) {
       return std::make_shared<hello_card>();
   }}
   ```
3. **Commit & Pull Request**: Ensure your code is warning-free, formatted, and passes all test suites before submitting a PR.
