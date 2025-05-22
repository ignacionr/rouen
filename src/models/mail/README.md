# Mail Model Refactoring (May 2025)

## Overview

This directory contains models and repositories for email metadata and related logic.

## Structure

- `metadata_repo.hpp` — Main repository for storing, retrieving, and querying email metadata. Now delegates JSON (de)serialization to helpers.
- `metadata_serialization.hpp` / `metadata_serialization.cpp` — Contains helpers for serializing and deserializing tags and action links as JSON.

## Refactoring Notes

- JSON (de)serialization logic for tags and action links was moved from `metadata_repo.hpp` to `metadata_serialization.hpp/cpp`.
- `metadata_repo.hpp` now uses these helpers for all serialization/deserialization.

## Best Practices

- Use the serialization helpers for all JSON operations on tags and action links.
- Keep repository logic focused on database access and business rules.
