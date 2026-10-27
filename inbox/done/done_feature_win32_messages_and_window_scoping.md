# Feature Request: Window Scoping & Win32 Message Dispatch for Process UI Actions

## Summary
Enhance Rouen's `/api/process/ui/action` endpoint (and UIA explorer) with:
1. **Window-level Scoping**: Ability to restrict control searches to a specific top-level or child window (`window_title`, `class_name`, or `hwnd`).
2. **Direct Win32 Message Dispatch**: Fallback or explicit control targeting via Win32 messages (`BM_CLICK`, `WM_COMMAND`, `WM_SETTEXT`, `WM_LBUTTONDOWN`/`UP` via `GetDlgItem` / `PostMessage`).
3. **Session-Independent Automation**: Avoid failing when the target Windows user session is disconnected (`state: Disc`), where global hardware input APIs (`SetCursorPos`, `mouse_event`) are ignored by the OS.

---

## Motivation & Problem Encountered

During automated operations of the Raptor Trading Platform test bench (`RaptorServer`, `RaptorClient`, `RaptorSCM`):

1. **Target Ambiguity Across Windows**:
   When a process has multiple windows open (e.g., main window `Institution-INZ - Raptor Client`, background tool window `FSM Manager`, and modal dialog `Raptor Client BETA build`), calling:
   ```json
   POST /api/process/ui/action
   {"target": "1", "action": "click"}
   ```
   walked the entire UIA tree and matched a text control named `"1"` (`id: "3222"`) inside `FSM Manager` instead of the `Accept` button (`id: "1"`) on the modal `#32770` dialog.
   Currently, there is no way to restrict an action to a specific window title or HWND.

2. **Legacy Win32 / MFC Controls**:
   Raptor and many enterprise trading suites use classic MFC/Win32 controls. Custom grids (e.g., `CMessageGrid` in `CServerLinkDialog`), toolbars, and dialog buttons often expose incomplete UIA pattern providers (only `Pane` without `InvokePattern` or `SelectionItemPattern`).
   However, Win32 control IDs (`IDC_*`) are stable and accessible cross-process via `GetDlgItem(hDlg, control_id)`.

3. **Disconnected Remote Desktop Sessions (`state: Disc`)**:
   In remote headless environments (e.g., workstations accessed over Rouen Mesh where RDP is detached), `query session` shows `Disc`. In this state, Windows disables the global hardware input stream: `SetCursorPos()` and `mouse_event()` in `ui_automation_explorer::click_at_coordinates` become no-ops.
   In contrast, `PostMessage(hCtrl, BM_CLICK, ...)` and `SendMessage(hCtrl, WM_SETTEXT, ...)` post directly to the window message queue and function 100% reliably regardless of desktop attachment.

---

## Proposed API Enhancements

### 1. Window Scoping Fields in `process_ui_request`

Add optional scoping parameters to `/api/process/ui/action`, `/api/process/ui/tree`, and `/api/process/ui/values`:

```json
{
  "run_id": "run-4-4",
  "window_title": "Raptor Client BETA build",   // Filter to window matching title substring
  "window_class": "#32770",                     // Optional class filter
  "hwnd": 984884,                               // Or direct decimal/hex HWND
  "target": "1",                                // Control ID or element name
  "action": "click"                             // click | set_value | focus | win32_click | win32_msg
}
```

### 2. Native Win32 Action Support (`action: "win32_click" | "win32_set_text" | "win32_msg"`)

When interacting with MFC/Win32 dialogs on Windows:
- If `action` is `win32_click`:
  Locate `HWND` of target control (via `GetDlgItem(hTargetWindow, atoi(target))` or UIA `NativeWindowHandle`), and post `BM_CLICK` (`0x00F5`) or `WM_LBUTTONDOWN` + `WM_LBUTTONUP`.
- If `action` is `win32_set_text`:
  Send `WM_SETTEXT` (`0x000C`) with the `value` string.
- If `action` is `win32_command`:
  Post `WM_COMMAND` (`0x0111`) with `WPARAM = control_id` directly to the parent dialog `HWND`.

---

## Value

- Completely eliminates the need to escape to PowerShell or external P/Invoke scripts when driving legacy MFC and Win32 applications.
- Guarantees 100% reliable execution in disconnected / headless test runner environments.
- Keeps all agent workflows cleanly contained within Rouen's REST API.
