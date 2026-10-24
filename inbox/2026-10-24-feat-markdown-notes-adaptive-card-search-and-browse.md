# Feature Request: Search and Browse Support in Markdown Notes Adaptive Card Presentation

**Date**: October 24, 2026  
**From**: Rouen Desktop Client UX & Mesh Engineering Team  
**To**: Rouen Architecture & Core Card Maintainers  
**Status**: Open (Feature Specification RFC)  
**Target Components**:
- Notes Card Implementation: [`src/cards/information/markdown_notes.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/information/markdown_notes.hpp)
- Notes Repository Model: [`src/models/notes/notes_repository.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/models/notes/notes_repository.hpp)
- Unit & Adaptive Interface Tests: [`tests/test_card_adaptive_interface.cpp`](file:///Users/ignaciorodriguez/src/rouen/tests/test_card_adaptive_interface.cpp)

**File Location**: [`inbox/2026-10-24-feat-markdown-notes-adaptive-card-search-and-browse.md`](file:///Users/ignaciorodriguez/src/rouen/inbox/2026-10-24-feat-markdown-notes-adaptive-card-search-and-browse.md)  

---

## 1. Executive Summary & Rationale

### 1.1 Overview
The Markdown Notes card (`src/cards/information/markdown_notes.hpp`) currently provides full search and browsing capabilities when rendered in traditional immediate-mode ImGui via its dedicated "Notes List" tab (`render_notes_tab()`). Users can filter notes by title, content, or tag, view the list of matching notes with timestamps, and click any item to load it into the editor.

However, the declarative **Adaptive Cards presentation** (`get_adaptive_card_json()`) only presents an editor form for a single note (`note_title`, `note_tags`, `note_content`, with action buttons for `save_note`, `delete_note`, `new_note`). It contains no visual elements or interactive mechanisms to:
1. Search or filter through the user's note collection.
2. Browse available notes with titles, tags, and timestamps.
3. Select and switch to a different note directly from the Adaptive Card interface.

This creates a major capability gap when viewing notes through the deck's Adaptive Card view mode, across remote Rouen Mesh card viewers (`mesh://<client-id>/notes`), or via web/companion client interfaces.

### 1.2 System Motivations
- **Remote Mesh Operability (`mesh://<client-id>/notes`)**: Remote operators accessing a peer workstation's notes over Rouen Mesh need to find and switch notes without having to switch rendering modes or rely on manual URI navigation (`notes:<title>`).
- **Deck Presentation Parity**: Desktop users who toggle on the "Adaptive Card View" mode should retain full functional parity with the ImGui multi-tab interface, including note discovery and browsing.
- **AI Agent & MCP Automation**: AI agents querying card state via `get_adaptive_card_json()` and dispatching actions via `handle_action()` require structured visibility into available notes and first-class action verbs to search and select them.

---

## 2. User Stories & Acceptance Criteria

### User Story 1: Search Notes in Adaptive Card
> **As a user viewing the Markdown Notes Adaptive Card**,  
> I want to enter a search query and/or tag filter and submit a search,  
> so that the card filters and displays matching notes in the note browser.

### User Story 2: Browse and Select Notes
> **As a user browsing notes in the Adaptive Card presentation**,  
> I want to see a list of recent or search-matched notes with their titles and tags,  
> and select any note to instantly load it into the note editor.

### User Story 3: Seamless Action Handling & State Synchronization
> **As a remote mesh client or automation agent**,  
> I want to dispatch `search_notes` and `select_note` actions via `handle_action()`,  
> so that the card state updates deterministically and reflects the chosen note across all views.

---

## 3. Technical Architecture & Requirements

### 3.1 Adaptive Card Schema Additions (`get_adaptive_card_json()`)
1. **Search & Filter Controls**:
   - Add an `Input.Text` control for search text (`id: "search_query"` or `"query"`) with placeholder `"Search notes by title or content..."`.
   - Optionally add a tag filter input (`id: "tag_filter"`).
   - Add an `Action.Submit` or `Action.Execute` button (`title: "Search"`, `verb: "search_notes"`).
   - Add an action button to clear search filters (`verb: "clear_search"` or `"refresh_notes"`).

2. **Note Browser / Results List**:
   - Render a container displaying matching notes from `notes_cache_` (or recent notes if no search filter is active).
   - For each note in the list:
     - Display title, tag badges, and last modified timestamp.
     - Include an action button (`title: "Open"`, `verb: "select_note"`) with payload containing `{"note_id": note.id, "title": note.title}` to switch the active note.
   - If no notes match the search query, display an informative placeholder `TextBlock` (e.g. `"No notes matching search query."`).

3. **Layout Considerations**:
   - Provide an intuitive layout:
     - A searchable notes browser section (e.g., above or collapsible alongside the editor).
     - Or an interactive presentation mode toggle / view switcher (e.g., `"Browse Notes"` vs. `"Edit Note"`).
   - Ensure the card remains compact, readable, and responsive across varying deck column widths.

### 3.2 Action Dispatch Handling (`handle_action()`)
Update `markdown_notes::handle_action(std::string_view action_json)` to support:
- `verb: "select_note"` / `verb: "load_note"`:
  - Parse `note_id` (integer or string) or `title` from action payload `data`.
  - Fetch the note from `repository_` and call `load_note(...)`.
  - Update `status_message_` and reset `last_adaptive_parse_time_` to trigger immediate re-parse.
- `verb: "search_notes"`:
  - Ensure search input values submitted from the form update `search_buffer_` and `tag_filter_buffer_`.
  - Query `repository_.list_notes(...)` and populate `notes_cache_`.
- `verb: "clear_search"`:
  - Clear search and filter buffers, reload full notes list into `notes_cache_`.

---

## 4. Verification & Testing Plan

1. **Unit Tests in `tests/test_card_adaptive_interface.cpp`**:
   - **Schema Verification**: Assert that `notes.get_adaptive_card_json()` contains search input definitions (`search_query`), search action verbs (`search_notes`), and note selection action elements (`select_note`).
   - **Search Action Test**: Dispatch `handle_action` with `verb: "search_notes"` and search criteria, verifying `notes_cache_` is filtered and subsequent card JSON reflects matching items.
   - **Selection Action Test**: Dispatch `handle_action` with `verb: "select_note"` and a target `note_id`, verifying that `get_current_note_id()`, `get_current_note_title()`, and editor contents reflect the selected note.
2. **Interactive UI Verification**:
   - Toggle "Adaptive Card View" in Rouen on the `notes` card.
   - Verify typing a query into the search input filters the displayed notes.
   - Verify clicking "Open" on a note in the browser list switches the active note in the editor form.
