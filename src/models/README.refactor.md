# Rouen Models Refactoring Log (May 2025)

## Summary of Major Refactoring

### RSS Models
- Moved `item` class and summary logic from `feed.hpp` to `feed_item.hpp/cpp`.
- Moved date parsing logic to `rss_date_parser.hpp/cpp`.
- Moved XML parsing logic to `feed_xml_parser.hpp/cpp`.
- Moved item-level SQLite logic from `sqliterepo.hpp` to `rss_item_repo.hpp`.

### Mail Models
- Moved JSON (de)serialization logic for tags and action links from `metadata_repo.hpp` to `metadata_serialization.hpp/cpp`.

### Git Models
- Moved process execution logic to `git_process_helper.hpp`.
- Moved repository scanning logic to `git_scanner.hpp`.

## DRY Principle
- Date parsing, JSON serialization, and process execution are now reusable utilities.
- Model headers are now focused on interfaces, with logic delegated to helpers.

## See Also
- `rss/README.md`
- `mail/README.md`
