# Feature Request: External Process / Window Screenshot Endpoint (`/api/process/ui/screenshot`)

## Summary
Extend Rouen's HTTP REST API to support capturing screenshots of external orchestrated processes and specific top-level Win32 windows/HWNDs, rather than only Rouen's internal SDL/ImGui desktop canvas.

## Motivation & Problem
During automated test bench operations on remote workstations (e.g., `ir-01` running `RaptorServer` and `RaptorClient`):
1. Rouen's process lifecycle APIs (`/api/process/start`, `/api/process/kill`, `/api/processes`) and UI automation endpoints (`/api/process/ui/action`, `/api/process/ui/values`) allow fully automated starting, logging in, and driving target applications.
2. However, when calling `/api/screenshot` with a target window name (e.g. `{"target": "order-admin - Raptor Client", "filename": "C:\\Temp\\..."}`), Rouen ignores the target window and defaults to `take_app_snapshot` in `src/editor/editor.hpp`. It captures only Rouen's own ImGui interface and returns `"Deck snapshot saved"`.
3. As a result, automated agents must currently break out of the REST API and invoke external PowerShell scripts (`PrintWindow` via P/Invoke) on the remote workstation just to obtain visual verification snapshots of the orchestrated application.

## Proposed API Specification

### Endpoint: `POST /api/process/ui/screenshot` (or extend `POST /api/screenshot`)

#### Request Body
```json
{
  "definition_id": 4,              // Optional: Orchestrated process definition ID
  "run_id": "run-4-4",             // Optional: Active run ID
  "pid": 29556,                    // Optional: Target process ID
  "hwnd": 1773798,                 // Optional: Specific window HWND (decimal or "0x...")
  "title_pattern": "Raptor Client",// Optional: Window title substring to match
  "filename": "C:\\Temp\\shot.png" // Target output path (PNG/BMP)
}
```

#### Response (200 OK)
```json
{
  "success": true,
  "file": "C:\\Temp\\shot.png",
  "hwnd": 1773798,
  "title": "order-admin - Raptor Client",
  "width": 800,
  "height": 600,
  "message": "Window snapshot captured successfully"
}
```

## Technical Implementation Notes (Windows)

On Windows, the implementation can leverage standard Win32 APIs:
1. **Window Resolution**:
   - Given `pid`, enumerate top-level windows belonging to that thread/process (`EnumWindows` + `GetWindowThreadProcessId` + `IsWindowVisible`).
   - If `title_pattern` is provided, filter via `GetWindowText`.
2. **Pixel Capture (No Screen Overlap / Background Safe)**:
   - Call `GetWindowRect(hwnd, &rect)`.
   - Create a compatible memory DC and bitmap.
   - Use `PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT)` (flag value `2`).
   - This captures the window's own rendered content even if minimized, off-screen, or obscured by other windows.
3. **Encoding**:
   - Save to disk via `Gdiplus::Bitmap` or `stbi_write_png`.

## Value
Enables 100% autonomous, end-to-end process orchestration and visual test verification exclusively through the Rouen REST API without requiring external scripting backdoors.
