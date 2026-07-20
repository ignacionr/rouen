# Rouen Project Rules

## Build System
- This project uses **Nix** as the build system. Always use `nix build` to compile.
- Do NOT use raw `cmake` commands to build. The CMakeLists.txt is consumed by the Nix flake.
- Before starting any task, read [README.md](README.md) and relevant docs if unfamiliar with the project conventions.

## Build Parallelism
- When compiling this project, limit parallel build jobs to at most 4 (e.g., use `make -j4`) because the memory on this computer cannot match a parallel build with all its cores.

## Notifications
- When a task is completed, use the macOS `say` command to announce a brief summary (e.g., `say "Build succeeded"` or `say "Changes applied to AI chat card"`). Use the default voice.

## Deployment
- The app is locally deployed to `$HOME/Applications/Rouen.app`. After a successful build, copy the compiled binary to `$HOME/Applications/Rouen.app/Contents/MacOS/rouen`.
- **Preserve the existing `.env` file** in `$HOME/Applications/Rouen.app/Contents/MacOS/.env` — do NOT overwrite it, as it contains user configuration.

