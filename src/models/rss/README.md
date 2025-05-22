# RSS Model Refactoring (May 2025)

## Overview

This directory contains the RSS feed and item models, as well as supporting utilities for parsing, date handling, and database access.

## Structure

- `feed.hpp` — Main RSS feed model. Now delegates item, date, and XML parsing to helpers.
- `feed_item.hpp` / `feed_item.cpp` — Contains the `feed_item` class (formerly `item` inner class in `feed.hpp`). Handles RSS item data and summary logic.
- `rss_date_parser.hpp` / `rss_date_parser.cpp` — Utility for robust date parsing from RSS/Atom feeds.
- `feed_xml_parser.hpp` / `feed_xml_parser.cpp` — Handles XML parsing for RSS and Atom feeds, populating feed and item data.
- `sqliterepo.hpp` — Handles feed-level SQLite operations (creation, update, delete, scan feeds).
- `rss_item_repo.hpp` — Handles item-level SQLite operations (batch insert, upsert, scan items).

## Refactoring Notes

- The `item` class and summary logic were moved from `feed.hpp` to `feed_item.hpp/cpp`.
- Date parsing logic was moved to `rss_date_parser.hpp/cpp` for reuse and clarity.
- XML parsing logic was moved to `feed_xml_parser.hpp/cpp`.
- Item-related database operations were moved from `sqliterepo.hpp` to `rss_item_repo.hpp`.

## Best Practices

- Use the helpers for date parsing and XML parsing to keep models concise.
- Prefer `feed_item` for all item data and summary operations.
- Use `rss_item_repo` for all item-level database access.
