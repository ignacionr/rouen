# Rouen Project Rules

## Build System
- Prefer local (nix-aware) incremental quick builds using:
  `nix develop --command cmake --build build --target rouen -j4`
  (If configuring or reconfiguring the `build` directory, use `nix develop --command cmake -B build -S .`).
- Use full `nix build --max-jobs 4` when validating full Nix derivation packages.
- Before starting any task, read [README.md](README.md) and relevant docs if unfamiliar with the project conventions.

## Build Parallelism
- When compiling this project, limit parallel build jobs to at most 4 (e.g., use `make -j4`) because the memory on this computer cannot match a parallel build with all its cores.

## Notifications
- When a task is completed, use the macOS `say` command to announce a brief summary (e.g., `say "Build succeeded"` or `say "Changes applied to AI chat card"`). Use the default voice.

## Deployment
- The app is locally deployed to `$HOME/Applications/Rouen.app`. After a successful build, copy the compiled binary to `$HOME/Applications/Rouen.app/Contents/MacOS/rouen`.
- **Preserve the existing `.env` file** in `$HOME/Applications/Rouen.app/Contents/MacOS/.env` — do NOT overwrite it, as it contains user configuration.
- **Mac ARM64 Code Signing Requirement**: After copying the binary, you must ad-hoc sign it using `codesign` so macOS does not terminate it on startup. Since `.env` is inside the `MacOS` directory, temporarily move it out before signing to prevent subcomponent structure errors, then restore it:
  `mv $HOME/Applications/Rouen.app/Contents/MacOS/.env /tmp/rouen_temp_env && codesign --force --sign - $HOME/Applications/Rouen.app/Contents/MacOS/rouen && mv /tmp/rouen_temp_env $HOME/Applications/Rouen.app/Contents/MacOS/.env`


