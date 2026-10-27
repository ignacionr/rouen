# Rouen Issue & Feature Inbox (`./inbox`)

This directory tracks active bug reports and feature specifications to be implemented in Rouen.

## Directory Structure & Conventions

- **`./inbox/<YYYY-MM-DD>-<type>-<slug>.md`**: **Open / Active** items (`type` is `bug` or `feat`). Only pending or in-progress reports belong at the top level of `./inbox/`.
- **`./inbox/done/done_<report>.md`**: **Completed / Resolved** items. Once an issue or feature is implemented, verified, committed, and pushed, its report is moved into `./inbox/done/` (with the `done_` prefix).
- **`./inbox/README.md`**: This documentation file (not an actionable issue).

> **Important for AI Agents**:
> - When asked to inspect or process `./inbox`, **only** process open `.md` reports located directly in `./inbox/` (excluding `README.md` and the `./inbox/done/` directory).
> - **Never** read, re-investigate, or re-process archived files inside `./inbox/done/` unless the user explicitly references a specific archived report by path.

## 9-Step Processing Workflow

When addressing an active report in `./inbox/<report>.md`:

1. **Understand**: Read the filed report and trace the affected architecture and code paths.
2. **Diagnose**: Pinpoint root causes across affected systems.
3. **Setup Unit Test**: Add or expand unit tests in `tests/` covering the bug or feature.
4. **Run and Fail**: Run the test to confirm it fails as expected (red).
5. **Fix**: Implement the clean, minimal fix or feature.
6. **Run and Pass**: Re-run the unit test and verify it passes (green), along with target builds (`-j2`) and local deployment.
7. **Commit**: Create a conventional commit (`fix(...)` or `feat(...)`).
8. **Push**: Push the commit to remote `origin`.
9. **Archive Report**: Update the report via `update_inbox_item` (or `git mv`) so it is moved to `inbox/done/done_<report>.md`.
