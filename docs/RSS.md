# Rouen RSS Reader Guide

Rouen ships a card-based RSS subsystem with local persistence, media extraction, playback resume watermarks, feed tagging, Smart Lists, and Git-syncable RSS configuration.

## Architecture

### UI cards
- `src/cards/information/rss.hpp`: RSS gallery/dashboard (subscriptions, tag filtering, search, AI discovery, Smart List entry points).
- `src/cards/information/rss_feed.hpp`: feed-level browser/editor (item list, per-feed tags, language override, delete flow).
- `src/cards/information/rss_item.hpp`: item reader/player (media playback, browser open, TTS fallback).
- `src/cards/information/rss_smart_list.hpp`: Smart List card and visual filter editor.

### Core runtime
- `src/hosts/rss_host.hpp`: central controller for feed loading, refresh scheduling, filtering, persistence bridge, watermark updates.
- `src/models/rss/feed.hpp`: RSS/Atom parsing (TinyXML2), media extraction hooks, `itunes:duration` parsing.
- `src/models/rss/sqliterepo.hpp`: SQLite schema, migrations, query and sync logic for feeds/items/tags/settings/smart lists.
- `src/models/rss/smart_list_filter.hpp`: typed DTOs for Smart List filter JSON.

## Key capabilities

### Feed ingestion and refresh
- Supports RSS 2.0 and Atom.
- Loads cached items on startup, then refreshes in background.
- Tracks per-feed backoff and adaptive timeout behavior.
- Resolves permanent redirects and updates stored feed URLs.

### Media extraction and duration enrichment
- Enclosure URLs are validated so images/thumbnails are not treated as playable media.
- `content:encoded` is merged for richer extraction when present.
- HTML media extraction detects embedded media (e.g. iframe mp4 URLs).
- Duration pipeline:
  1. Use feed metadata (`itunes:duration`) when available.
  2. For YouTube/Shorts links, probe watch-page metadata (`lengthSeconds` / `approxDurationMs`).
  3. For MP4 URLs, probe container headers quickly.
  4. If duration is still unknown, fallback to `ffprobe` (when available on PATH/Nix profile).

### Smart Lists
- Smart Lists are persisted and represented as `filter_group` + `filter_condition` JSON.
- Supported fields:
  - `title`
  - `description`
  - `pub_date`
  - `media_duration_seconds`
  - `feed_tag`
- Supported operators:
  - Text: `CONTAINS`, `EXCLUDES`, `MATCHES`, `==`, `!=`
  - Numeric/date: `>`, `<`, `>=`, `<=`, `==`, `!=`
  - Tag sets: `IN`, `NOT IN`
- Group logic supports `AND` / `OR`.
- Relative date values include `Today`, `Last <N>h`, `Last <N> days`, `Last <N> min`.
- Duration filter behavior: missing duration is treated as `0` in numeric comparisons so fresh items without resolved duration are still eligible for short-content lists.

### Tagging and language metadata
- Feeds have many-to-many tags via `feed_tag`.
- Default tag definitions are seeded in DB (`News`, `Tech / Dev`, `Podcasts`, etc.).
- Newly loaded feeds without tags are auto-classified.
- Per-feed language overrides are stored and synced.

### Watermarks and playback continuity
- Item watermark is persisted per `(feed_id, link, title)` and mirrored in memory.
- Playback stop/exit paths update watermark so resume state is preserved.

### Sync/export model
- RSS sync directory exports:
  - `feeds.json` (subscriptions, title, image, language, tags)
  - `smart_lists.json` (title + filter definition)
- Imports are two-way sync oriented:
  - upsert existing items
  - remove local entries missing from imported dataset

## Database notes

Main schema (in `sqliterepo.hpp`) includes:
- `feed`
- `item`
- `feed_tag`
- `rss_tag_definition`
- `settings`
- `smart_list`

Important item fields:
- `pub_date`
- `watermark`
- `media_duration_seconds`

## Card behavior highlights

### RSS gallery (`rss:`)
- Tag pills with freshness-aware visual emphasis.
- Search over cached feed/item data.
- Smart List shortcuts that open `rss-smart-list:<title>`.
- Feed refresh controls and AI feed discovery UX.

### Feed card (`rss-feed:*`)
- Inline item browsing with lazy loading.
- Tag management and language override editor.
- Per-feed refresh + delete actions.

### Smart List card (`rss-smart-list:<title>`)
- Asynchronous filtered item loading.
- Visual filter builder (field/operator/value rows).
- Save/rename/delete Smart List operations.
- Optional AI-generated Smart List title.

## Testing

Relevant tests:
- `tests/test_rss_media_extraction.cpp`
- `tests/test_rt_media.cpp`
- `tests/test_rss_watermark.cpp`

Typical workflow:

```bash
nix develop
cmake -S . -B build -DROUEN_BUILD_TESTS=ON
cmake --build build --parallel
```

or for standalone tests project:

```bash
cmake -S tests -B build-tests
cmake --build build-tests --parallel
```
