# Bug: `/api/process/ui/set-value` and `/api/process/ui/focus` Discard Action and Default to Click

## Summary
The shortcut HTTP endpoints `POST /api/process/ui/set-value` and `POST /api/process/ui/focus` discard the assigned `req.action` value because they pass the raw `mg_http_message* hm` to `handle_process_ui_action`, which re-parses the JSON request from scratch. Since `action` is absent from the incoming JSON payload in shortcut requests, it falls back to `"click"`. Consequently, all shortcut endpoints perform a click instead of their intended action.

---

## Affected Code

In `src/hosts/api_server_host.cpp` (lines 2292–2311):

```cpp
std::string api_server_host::handle_process_ui_click(struct mg_connection* c, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);
    return handle_process_ui_action(c, hm); // Passes hm, ignores req
}

std::string api_server_host::handle_process_ui_set_value(struct mg_connection* c, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);
    req.action = "set_value";               // Sets action on local 'req'...
    return handle_process_ui_action(c, hm); // ...but passes 'hm' which does not contain action!
}

std::string api_server_host::handle_process_ui_focus(struct mg_connection* c, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);
    req.action = "focus";                   // Sets action on local 'req'...
    return handle_process_ui_action(c, hm); // ...but passes 'hm' which does not contain action!
}
```

And in `handle_process_ui_action(struct mg_connection* /*c*/, struct mg_http_message* hm)`:

```cpp
std::string api_server_host::handle_process_ui_action(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);   // Re-populates req from hm!
    ...
    std::string action = req.action.empty() ? "click" : req.action;
```

---

## Steps to Reproduce

1. Launch an orchestrated process with a text field (e.g., RaptorClient logon edit box `target: "1001"`).
2. Call `POST /api/process/ui/set-value` with payload:
   ```json
   {
     "run_id": "run-4-4",
     "target": "1001",
     "value": "institution-admin"
   }
   ```
3. Inspect the returned JSON response.

### Expected Behavior
`"action_performed": "set_value"` and the text input field has its value updated.

### Actual Behavior
The response returns:
```json
{
  "action_performed": "click_at_bounds",
  "matched_element_id": "1001",
  "matched_element_name": "Logon name:",
  "pid": 35448,
  "success": true
}
```
The edit box is merely clicked and focused, but no value is set.

---

## Proposed Fix

Refactor `handle_process_ui_action` to accept an already-populated `const process_ui_request& req`, or a helper overload:

```cpp
std::string api_server_host::execute_process_ui_action(const process_ui_request& req) {
    int64_t pid = resolve_process_pid(req.run_id, req.definition_id, req.pid);
    if (pid <= 0) {
        error_response resp{"Valid running process identifier required"};
        return glz::write_json(resp).value_or(R"({"error":"Valid running process identifier required"})");
    }

    rouen::helpers::ui_manipulation_result res;
    if (req.target.empty() && (req.x > 0.0f || req.y > 0.0f)) {
        res = rouen::helpers::ui_automation_explorer::click_at_coordinates(pid, req.x, req.y);
    } else {
        std::string action = req.action.empty() ? "click" : req.action;
        res = rouen::helpers::ui_automation_explorer::perform_control_action(pid, req.target, action, req.value);
    }
    ...
}

std::string api_server_host::handle_process_ui_action(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);
    return execute_process_ui_action(req);
}

std::string api_server_host::handle_process_ui_set_value(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);
    req.action = "set_value";
    return execute_process_ui_action(req);
}

std::string api_server_host::handle_process_ui_focus(struct mg_connection* /*c*/, struct mg_http_message* hm) {
    process_ui_request req;
    populate_process_ui_request(req, hm);
    req.action = "focus";
    return execute_process_ui_action(req);
}
```
