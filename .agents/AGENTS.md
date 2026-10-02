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
- When a task is completed, use the macOS `say` command to announce a brief summary (e.g., `say "Build succeeded"` or `say "Changes applied to AI chat card"`). Do NOT pass `-v` flags as explicit voice flags fall back to robotic compact legacy engines; let `say` use the default system voice.

## Deployment
- The app is locally deployed to `$HOME/Applications/Rouen.app`. After a successful build, copy the compiled binary to `$HOME/Applications/Rouen.app/Contents/MacOS/rouen`.
- **Preserve the existing `.env` file**: Store user `.env` configuration in `$HOME/Applications/Rouen.app/Contents/Resources/.env` (where ConfigService looks up bundle resources). Plain text files inside `Contents/MacOS/` invalidate macOS bundle code signatures.
- **Mac ARM64 Code Signing Requirement**: Clean any `.rouen-wrapped` leftovers, sign `libpdfium.dylib`, and ad-hoc sign `rouen` with explicit designated requirement so macOS TCC Accessibility permissions persist across recompiles:
  `cp build/rouen.app/Contents/MacOS/rouen $HOME/Applications/Rouen.app/Contents/MacOS/rouen && ([ ! -f $HOME/Applications/Rouen.app/Contents/MacOS/.env ] || cp $HOME/Applications/Rouen.app/Contents/MacOS/.env $HOME/Applications/Rouen.app/Contents/Resources/.env) && rm -f $HOME/Applications/Rouen.app/Contents/MacOS/.env $HOME/Applications/Rouen.app/Contents/MacOS/.rouen-wrapped && chmod +x $HOME/Applications/Rouen.app/Contents/MacOS/libpdfium.dylib 2>/dev/null || true && codesign --force --sign - $HOME/Applications/Rouen.app/Contents/MacOS/libpdfium.dylib && codesign --force --sign - --requirements '=designated => identifier "com.rouen.app"' $HOME/Applications/Rouen.app/Contents/MacOS/rouen`



## LLM Backend & Model Querying
- Never guess or hardcode static LLM model identifiers when configuring or suggesting LLM providers.
- Always query the LLM backend endpoint (e.g. `GET /v1/models` or `GET /v1beta/models`) to retrieve the list of active available models dynamically.

## Issue & Bug Fix Workflow (Inbox)
- When addressing bugs or feature requests filed in `./inbox`:
  1. **Understand**: Read the filed report and trace the affected architecture and code paths.
  2. **Diagnose**: Pinpoint root causes across affected systems.
  3. **Setup Unit Test**: Add or expand unit tests in `tests/` covering the bug/feature.
  4. **Run and Fail**: Run the test to confirm it fails as expected (red).
  5. **Fix**: Implement the clean, minimal fix.
  6. **Run and Pass**: Re-run the unit test and verify it passes (green), along with target builds and deployment.
  7. **Commit**: Create a conventional commit (`fix(...)` or `feat(...)`).
  8. **Push**: Push commit to remote `origin`.
  9. **Rename Report**: Rename `inbox/<report>.md` to `inbox/done_<report>.md`.

## Mesh Infrastructure & Node Roles
- **Primary / Always-On Node**: The MacMini node (`rouen-desktop-mac` / `Ignacios-Mac-mini.local`) is currently the most stable Rouen node on the mesh. It is always-on and has a reliable, permanent internet connection.
- **Service Anchoring (e.g. Telegram)**: Long-polling background services and gateway roles (such as the Telegram bot daemon connection) should naturally anchor and run on the MacMini instance. Roaming nodes (such as the MacBook Air) and remote workstations discover and route urgent notifications and alerts through the MacMini gateway via the Rouen Mesh ephemeral registry (`telegram/presence/*`).


