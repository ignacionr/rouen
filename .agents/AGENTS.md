# Rouen Project Rules

## Build System
- Prefer local (nix-aware) incremental quick builds using Ninja:
  `nix develop --command cmake --build build --target rouen -j2`
  (If configuring or reconfiguring the `build` directory, use `nix develop --command cmake -G Ninja -B build -S .`).
- Always prefer Ninja generator (`-G Ninja`) for CMake builds.
- Use full `nix build --max-jobs 2` when validating full Nix derivation packages.
- Before starting any task, read [README.md](README.md) and relevant docs if unfamiliar with the project conventions.

## Build Parallelism
- **CRITICAL**: When compiling this project, STRICTLY limit parallel build jobs to at most 2 (e.g., `-j2` or `--max-jobs 2`). Never omit `-j` or set `-j` higher than 2. The host machine has 16 GB RAM and heavy C++ compilation memory usage (~4 GB per job) will cause complete system memory exhaustion and force a hard reboot.

## Notifications
- When a task is completed, use the macOS `say` command to announce a brief summary (e.g., `say "Build succeeded"` or `say "Changes applied to AI chat card"`). Use the default voice.

## Deployment
- The app is locally deployed to `$HOME/Applications/Rouen.app`. After a successful build, copy the compiled binary to `$HOME/Applications/Rouen.app/Contents/MacOS/rouen`.
- **Preserve the existing `.env` file**: Store user `.env` configuration in `$HOME/Applications/Rouen.app/Contents/Resources/.env` (where ConfigService looks up bundle resources). Plain text files inside `Contents/MacOS/` invalidate macOS bundle code signatures.
- **Mac ARM64 Code Signing Requirement**: Clean any `.rouen-wrapped` leftovers, sign `libpdfium.dylib`, and ad-hoc sign `rouen` with explicit designated requirement so macOS TCC Accessibility permissions persist across recompiles:
  `cp build/rouen.app/Contents/MacOS/rouen $HOME/Applications/Rouen.app/Contents/MacOS/rouen && ([ ! -f $HOME/Applications/Rouen.app/Contents/MacOS/.env ] || cp $HOME/Applications/Rouen.app/Contents/MacOS/.env $HOME/Applications/Rouen.app/Contents/Resources/.env) && rm -f $HOME/Applications/Rouen.app/Contents/MacOS/.env $HOME/Applications/Rouen.app/Contents/MacOS/.rouen-wrapped && chmod +x $HOME/Applications/Rouen.app/Contents/MacOS/libpdfium.dylib 2>/dev/null || true && codesign --force --sign - $HOME/Applications/Rouen.app/Contents/MacOS/libpdfium.dylib && codesign --force --sign - --requirements '=designated => identifier "com.rouen.app"' $HOME/Applications/Rouen.app/Contents/MacOS/rouen`


## Windows Environment & Dev Tools
- On this computer (Windows), developer tools are located in the Visual Studio 2022 installation directory (`C:\Program Files\Microsoft Visual Studio\2022\Professional\`):
  - **Git**: `C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe`
  - **CMake**: `C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - **Ninja**: `C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
