# Feature Request: Externalize Default Persona Definitions to Bundled JSON & Markdown Resources (Option 1A)

- **Date**: 2026-11-01
- **Type**: `feat`
- **Status**: `done`
- **Component**: `src/helpers/persona_manager.hpp`, `src/helpers/platform_utils.hpp`, `resources/personas/`, `cmake/`

---

## 1. Problem Statement

`src/helpers/persona_manager.hpp` currently embeds over 600 lines of escaped C++ string literals for built-in AI persona metadata and multi-paragraph system prompts across `setup_default_personas()` and `sanitize_and_migrate_personas()`. Because `persona_manager.hpp` is included across 9 translation units (`ai_chat.cpp`, `mcp_host.cpp`, `llm_host.cpp`, `universal_sync_host.cpp`, etc.):

1. Modifying any word in a built-in persona prompt or configuration forces a heavy C++ recompilation.
2. Multi-paragraph Markdown prompts are duplicated between `setup_default_personas()` and `sanitize_and_migrate_personas()` as escaped C++ string literals.
3. Authoring or reviewing system prompts inside a C++ header is error-prone compared to standalone `.md` and `.json` files.

---

## 2. Proposed Architecture (Option 1A: `resources/personas/<slug>.json` + `<slug>.md`)

1. **Bundled Resource Directory (`resources/personas/`)**:
   - Store built-in persona metadata in `resources/personas/<slug>.json` (using `Persona`'s Glaze schema: `name`, `description`, `allowed_mcps`, `llm_config_name`, `enable_search`, `allowed_personas`, `temperature`, `thinking_level`, `max_tool_iterations`, `max_output_tokens`, `context_compaction_strategy`, `max_context_turns`, `preserve_recent_turns`).
   - Store each built-in persona's system prompt in a companion `resources/personas/<slug>.md` file as clean, unescaped Markdown text.
   - Provide `resources/personas/manifest.json` listing the canonical ordered array of built-in persona slugs so default ordering (with `rouen-assistant` at index `0`) is deterministic across all platforms and filesystems.

2. **Resource Discovery & Loading in `PersonaManager`**:
   - Enhance `rouen::platform::get_resource_path()` in `src/helpers/platform_utils.hpp` to also search two levels above the executable directory (`exec_path.parent_path().parent_path()`) so test binaries located in `build/tests/` resolve `<repo_root>/resources/personas/` regardless of working directory.
   - Add `PersonaManager::load_persona_from_resource_files(json_path, md_path)` and `PersonaManager::load_default_personas_from_resources()` to load all built-in personas from `resources/personas/manifest.json` + `<slug>.json` + `<slug>.md` (or fall back to scanning `personas/*.json` + `*.md` in a custom directory).
   - Replace the hardcoded C++ string literals in `setup_default_personas()` and `sanitize_and_migrate_personas()` with resource-driven loading and synchronization:
     - `setup_default_personas()` populates `personas_` directly from `load_default_personas_from_resources()`.
     - `sanitize_and_migrate_personas()` compares existing personas against the authoritative resource definitions from `load_default_personas_from_resources()`, ensuring all built-in personas exist and upgrading their `llm_config_name`, `temperature`, `thinking_level`, `max_tool_iterations`, `max_output_tokens`, `description`, `system_prompt`, `allowed_mcps`, and `allowed_personas` from the resource files without hardcoded C++ prompt strings.

3. **CMake, Bundle Packaging & Installer Scripts**:
   - Update `cmake/macos.cmake`, `cmake/linux.cmake`, and `cmake/windows.cmake` to copy `resources/personas/*.json` and `resources/personas/*.md` into the application bundle / build directory (`Contents/Resources/personas/` on macOS, `personas/` on Linux and Windows).
   - Update Windows WiX MSI definitions (`installer/rouen.wxs` and `installer/rouen-v4.wxs`) with a `PersonasFolder` (`INSTALLFOLDER\personas`) and `PersonasAssets` component (with per-user registry `KeyPath`, `RemoveFolder` cleanup, and all built-in `.json`/`.md` resources), plus dynamic MSI injection and smoke-test verification in `.github/workflows/windows-release.yml` and `scripts/validate-msi-config.sh`.
   - Update release workflows (`.github/workflows/macos-release.yml`, `.github/workflows/release.yml`), Linux Nix `installPhase` (`flake.nix`), and self-upgrade scripts (`scripts/upgrade-rouen.ps1`, `scripts/upgrade-windows-msi.ps1`, `scripts/upgrade-rouen.sh`) to bundle, back up, and deploy `personas/`.

---

## 3. Acceptance Criteria & Verification

1. `src/helpers/persona_manager.hpp` contains no multi-line hardcoded prompt blocks in `setup_default_personas()` or `sanitize_and_migrate_personas()`.
2. All 15 built-in personas have corresponding `<slug>.json` and `<slug>.md` files in `resources/personas/`, indexed in `resources/personas/manifest.json`.
3. Unit tests in `tests/test_gemini_adapter.cpp` verify:
   - `resources/personas/manifest.json` and every `<slug>.json` + `<slug>.md` pair exist and deserialize cleanly via `PersonaManager::load_default_personas_from_resources()`.
   - Loading a persona from a `<slug>.json` + `<slug>.md` resource pair populates `system_prompt` from the `.md` file when `system_prompt` is omitted/empty in JSON.
   - All existing `PersonaManagerTest` suites pass (`ThinkingLevelConfigurationAndDefaults`, `AutonomousEngineerPersonaRegistrationAndMCPs`, `AutonomousEngineerExtendedIterationsAndTokens`, `RouenAssistantTaskExecutionDelegationRule`, `ImportFromDirectoryPreservesAndMigratesCorePersonaIntegrity`, `AdaptiveCardArchitectCapabilities`, `PersonaArchitectRegistrationAndCapabilities`).
4. Full build (`-j2`) and deployment to `$HOME/Applications/Rouen.app` succeed with `Contents/Resources/personas/` populated.
