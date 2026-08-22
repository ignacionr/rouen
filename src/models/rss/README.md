# RSS Model Module

This module contains parsing, filter DTOs, and SQLite persistence for Rouen RSS data.

## What this module is responsible for

- Parsing RSS/Atom payloads into normalized feed/item models.
- Extracting media candidates from enclosures and HTML content.
- Parsing and storing item media duration metadata.
- Persisting feeds, items, tags, settings, and Smart Lists in SQLite.
- Exporting/importing RSS sync payloads (`feeds.json`, `smart_lists.json`).

## Core files

- `feed.hpp`: main parser for RSS/Atom channels/entries.
- `feed_item.hpp/.cpp`: feed item model (`watermark`, `media_duration_seconds`, media selection helpers).
- `feed_xml_parser.hpp/.cpp`: XML helper logic.
- `rss_date_parser.hpp/.cpp`: resilient date parsing for feed formats.
- `smart_list_filter.hpp`: Smart List filter schema DTOs.
- `sqliterepo.hpp`: schema setup, migrations, filtered queries, sync import/export.

## Media duration strategy

Duration can come from several sources, in this order:

1. `itunes:duration` parsed from the feed XML.
2. YouTube watch-page probing (`lengthSeconds` / `approxDurationMs`) for YouTube and Shorts links.
3. Direct MP4 box probing for fast duration extraction.
4. `ffprobe` fallback (if executable is available) for items still missing feed-provided duration.

Durations are persisted in `item.media_duration_seconds`.

## Smart List filtering details

`scan_filtered_items` composes SQL dynamically using `filter_group` definitions:

- Fields: `title`, `description`, `pub_date`, `media_duration_seconds`, `feed_tag`
- Group operators: `AND`, `OR`
- Supported operations: comparison, text ops (`CONTAINS`, `EXCLUDES`, `MATCHES`), and tag set ops (`IN`, `NOT IN`)

Date filters support relative values (e.g. `Today`, `Last 24h`, `Last 7 days`).

For numeric duration comparisons, items with missing (`NULL`) or non-positive (`<= 0`) duration values are excluded from upper-bounded duration Smart Lists (e.g., `<= 300` seconds) to prevent unprobed or long-duration media items from improperly matching short-duration filters.

## Persistence and sync

SQLite entities include:
- `feed`
- `item`
- `feed_tag`
- `rss_tag_definition`
- `settings`
- `smart_list`

Sync export/import uses:
- `feeds.json` for subscriptions (+ tags/language)
- `smart_lists.json` for Smart List definitions

## Related runtime integration

The host/controller (`src/hosts/rss_host.hpp`) orchestrates this module:
- fetches feeds
- calls parser/model logic
- enriches missing metadata (including duration fallback)
- writes to `sqliterepo`
- serves filtered/feed/item data to UI cards
