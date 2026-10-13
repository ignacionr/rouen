# Bug: Universal Mesh Sync Exponential Conflict Loop and Registry Explosion

## Summary

Following the rollout of Universal Mesh Sync with periodic incremental reconciliation in Rouen v1.4.16, the Mesh Persistent Registry (`sync/v1/`) and local `notes.db` databases across connected nodes experienced an **exponential conflict explosion**.

In under an hour, **1,140 recursively nested conflict notes** were generated from only **9 legitimate notes**, multiplying every ~5 minutes:
```
sync/v1/notes/test-conflict-2026-10-04-15-18-06-conflict-2026-10-04-15-23-10-conflict-2026-10-04-15-28-16-conflict-2026-10-04-15-33-28-conflict-2026-10-04-15-38-53.md
```

Nodes participating in the loop include:
- `rouen-ignacios-macbook-air` (macOS)
- `rouen-ignacios-mac-mini` (macOS)
- `rouen-md-d-ws-ir-01` (Windows / WSL)
- `rouen-md-d-lt-ir-01` (Windows / WSL)

---

## Root Cause Analysis

The explosion is caused by a feedback loop between three compounding flaws:

### 1. Stale / Unmaintained `notes_last_sync` Metadata

In [`src/models/notes/notes_repository.hpp#L461-L465`](file:///Users/inz/src/rouen/src/models/notes/notes_repository.hpp#L461-L465):

```cpp
const std::string last_sync = get_sync_meta("notes_last_sync");
...
bool local_conflict = false;
if (local_note.has_value() && local_note->content != markdown && !last_sync.empty()) {
    local_conflict = is_timestamp_newer(local_note->updated_at, last_sync) && source_hash != known_hash;
}
```

- In the legacy Git sync scheme, `notes_last_sync` was updated whenever a sync completed.
- In [`src/hosts/universal_sync_host.cpp`](file:///Users/inz/src/rouen/src/hosts/universal_sync_host.cpp#L463-L466), `import_all_local_datasets()` calls `notes_repo.import_from_directory()`, but **never updates `notes_last_sync`** in `sync_metadata`.
- On nodes with an existing `notes_last_sync` from a previous Git sync (e.g. `2026-06-14 09:34:03` on `macmini`), every note modified or touched since June had `local_note->updated_at > last_sync`. As a result, `is_timestamp_newer(local_note->updated_at, last_sync)` evaluated to `true` permanently.

### 2. Cross-Platform Line Endings (CRLF vs LF) and Serialization Asymmetry

- The mesh network contains both Windows nodes (`ws-ir-01`, `lt-ir-01`) and macOS nodes (`macbook-air`, `macmini`).
- When Windows exports markdown files to disk, standard streams write `\r\n` (CRLF), whereas macOS writes `\n` (LF).
- Furthermore, [`export_to_directory()`](file:///Users/inz/src/rouen/src/models/notes/notes_repository.hpp#L396) writes:
  ```cpp
  output << "# " << note.title << "\n\n";
  if (!note.tags.empty()) {
      output << "<!-- tags:" << note.tags << " -->\n\n";
  }
  output << note.content;
  ```
  While [`import_from_directory()`](file:///Users/inz/src/rouen/src/models/notes/notes_repository.hpp#L434-L444) consumes `# Title\n` with `std::getline` and removes only a single leading `\n`:
  ```cpp
  if (!markdown.empty() && markdown.front() == '\n') {
      markdown.erase(markdown.begin());
  }
  ```
  When `tags` is empty, this leaves an extra leading newline attached to `markdown`.
- Consequently, `local_note->content != markdown` and `source_hash != known_hash` evaluate to `true` on almost every sync pass.

### 3. Cascading Two-Way Sync Conflict Recursion

In [`src/models/notes/notes_repository.hpp#L466-L471`](file:///Users/inz/src/rouen/src/models/notes/notes_repository.hpp#L466-L471):

```cpp
if (local_conflict) {
    const std::string conflict_title = std::format("{} (conflict {})", note_title, now_timestamp());
    save_note(conflict_title, markdown, tags);
} else {
    save_note(note_title, markdown, tags);
}
```

1. When `local_conflict` triggers during `sync_in_mesh()`, `save_note()` inserts a **new note** titled `<title> (conflict <timestamp>)` into the local SQLite database.
2. In the very same two-way sync cycle, [`sync_twoway_mesh()`](file:///Users/inz/src/rouen/src/hosts/universal_sync_host.cpp#L578-L581) immediately runs [`sync_out_mesh()`](file:///Users/inz/src/rouen/src/hosts/universal_sync_host.cpp#L503-L555).
3. `export_all_local_datasets()` dumps the newly generated conflict note to disk, and `sync_out_mesh()` publishes it to the Mesh Registry as `sync/v1/notes/<slug>-conflict-<timestamp>.md`.
4. 5 minutes later (`periodic_interval_seconds = 300`), peer nodes pull down this new conflict note. Because of the line ending/whitespace mismatch and stale `notes_last_sync`, the peer node treats the *conflict note itself* as conflicting, generating:
   `<title> (conflict ...) (conflict ...)`.
5. With multiple nodes exchanging notes every 300 seconds, conflict notes multiplied geometrically ($N \times 4$ per cycle), reaching 1,140 entries in under an hour.

---

## Current Mesh State

- **Mesh Persistent Registry** (`s1.inz.dev` / `s2.inz.dev`):
  - Total items: 1,187
  - Legitimate items: 47 (9 notes, 1 contact, 15 personas, 2 rss, series, etc.)
  - Conflict keys: **1,140 keys** matching `sync/v1/notes/*conflict*`
- **Local SQLite databases (`notes.db`)**:
  - `rouen-ignacios-mac-mini`: 1,249 conflict notes
  - `rouen-md-d-ws-ir-01`: ~900+ conflict notes
  - `rouen-md-d-lt-ir-01`: ~912 conflict notes
  - `rouen-ignacios-macbook-air`: 567 conflict notes

---

## Remediation Plan

### 1. Code Fixes in `rouen`

1. **Normalize Line Endings and Content Trimming**:
   - Before hashing and comparing note content in `notes_repository.hpp`, normalize `\r\n` to `\n` and strip trailing whitespace.
   - Symmetrically parse the markdown header in `import_from_directory()` to avoid introducing phantom leading/trailing newlines.
2. **Update `notes_last_sync` in Universal Sync**:
   - In `UniversalSyncHost::import_all_local_datasets()`, invoke `notes_repo.set_sync_meta("notes_last_sync", now_timestamp())` after successful import.
3. **Prevent Recursive Conflict Cycles**:
   - Guard against creating conflict duplicates of conflict notes: if `note_title.contains(" (conflict ")` or filename contains `-conflict-`, resolve via Last-Write-Wins (LWW) or update in place instead of creating another nested `(conflict ...)` layer.

### 2. Registry & Database Cleanup

1. **Purge Mesh Registry**:
   - Execute an administrative script calling `POST /admin/api/registry/delete` on `rouen-service` for all keys matching `sync/v1/notes/*conflict*`.
2. **Clean Local Databases**:
   - On each affected node, clean up `notes.db`:
     ```sql
     DELETE FROM notes WHERE title LIKE '%conflict%';
     DELETE FROM note_file_hashes WHERE note_title LIKE '%conflict%';
     DELETE FROM note_links WHERE source_note_id NOT IN (SELECT id FROM notes) OR target_note_id NOT IN (SELECT id FROM notes);
     ```
   - Clear staging folders (`~/Library/Application Support/Rouen/rouen-mesh-sync/notes/*conflict*` or Linux/Windows equivalent).
