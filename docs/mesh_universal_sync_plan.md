# Mesh-Backed Persistent Registry Universal Sync Architecture & Implementation Plan

## 1. Executive Summary

This specification describes transitioning Rouen's Universal Sync engine from a legacy Git-based remote repository synchronization scheme to a high-speed, zero-dependency, Mesh-backed persistent key-value registry synchronization scheme.

When a Rouen instance is paired with the Rouen Mesh (`ROUEN_MESH_PAIRED=1`), this new Mesh-backed persistent registry engine supercedes Git Sync. For non-paired instances, legacy Git-based synchronization remains operational as a fallback.

All data payloads in the Mesh Registry are protected using **Client-Side End-to-End Encryption (E2EE)** with AES-256-GCM and PBKDF2 key derivation, ensuring zero-trust data privacy on relay cluster nodes (`rouen-service`).

---

## 2. Key Architecture Decisions

1. **Automatic Supercedence**:
   - If `rouen_mesh_host::is_paired()` is true, Universal Sync automatically directs all sync operations (`sync_in`, `sync_out`, `sync_twoway`, and granular hooks) to the Mesh Persistent Registry.
   - If not paired, it delegates to `GitSyncHost`.
2. **Granular Real-Time Synchronization**:
   - Entities are identified individually under the `sync/v1/` prefix:
     - `sync/v1/notes/{slug}`
     - `sync/v1/contacts/{uuid}`
     - `sync/v1/personas/{slug}`
     - `sync/v1/travel/{slug}`
     - `sync/v1/rss/feeds`
     - `sync/v1/series/{id}`
     - `sync/v1/cards/{id}`
     - `sync/v1/objectives/{name}`
     - `sync/v1/config/{file}`
   - Saving or mutating an item locally triggers immediate background publication of that key to the Mesh Registry with `is_ephemeral = false`.
3. **Client-Side End-to-End Encryption (E2EE)**:
   - Data payloads are encrypted using AES-256-GCM with a 12-byte cryptographically random IV and a 16-byte authentication tag via OpenSSL EVP.
   - The 256-bit symmetric key is derived from a user-configured sync passphrase and salt using `PKCS5_PBKDF2_HMAC` (SHA-256, 100,000 iterations).
4. **Deletion Handling (Tombstones)**:
   - To reliably communicate deletions across offline and asynchronous devices, deleted items publish a tombstone envelope:
     ```json
     {
       "version": 1,
       "dataset": "contacts",
       "key": "contact_abc123",
       "updated_at": 1728054100,
       "deleted": true
     }
     ```
   - On sync, peers receiving a tombstone delete the record from their local SQLite/file store.
5. **Conflict Resolution**:
   - Last-Write-Wins (LWW) based on `updated_at` timestamps.
6. **Server Coordination**:
   - An RFC is submitted to `rouen-service/inbox/2026-10-04-rfc-mesh-persistent-registry-storage.md` requesting disk-backed persistence (e.g. SQLite) for `is_ephemeral == false` registry keys so data survives server restarts.

---

## 3. Implementation Roadmap

### Phase 1: Client-Side Crypto & Envelope Engine (`SyncCryptoService`)
- [ ] Create `src/helpers/sync_crypto_service.hpp` and `src/helpers/sync_crypto_service.cpp`.
- [ ] Implement PBKDF2 (SHA-256) key derivation from passphrase and salt.
- [ ] Implement OpenSSL EVP AES-256-GCM encryption and decryption.
- [ ] Implement envelope serialization/deserialization with Glaze.
- [ ] Add unit test suite in `tests/test_sync_crypto.cpp` validating envelope encryption, decryption, authentication tag verification, and tombstone structures.

### Phase 2: Mesh Sync Engine & Lifecycle in `UniversalSyncHost`
- [ ] Update `UniversalSyncHost` to detect mesh pairing status (`is_mesh_sync_active()`).
- [ ] Implement `sync_item(dataset, key, content, is_deleted)`.
- [ ] Implement `sync_in_mesh()`, `sync_out_mesh()`, and `sync_twoway_mesh()`.
- [ ] Reconcile local datasets with Last-Write-Wins logic and tombstone processing.
- [ ] Add unit tests in `tests/test_universal_mesh_sync.cpp`.

### Phase 3: Granular Real-Time Publish Hooks
- [ ] Wire granular publish hooks into Contact Card & Contacts Repository (`sync_item("contacts", ...)`).
- [ ] Wire granular publish hooks into Persona Manager & Personas (`sync_item("personas", ...)`).
- [ ] Wire granular publish hooks into Markdown Notes Card & Repository (`sync_item("notes", ...)`).
- [ ] Wire granular publish hooks into Travel, RSS, Series, and Objectives mutators.

### Phase 4: UI & Mode Switching in Universal Sync Card (`sync_card.hpp`)
- [ ] Add dynamic UI toggle in `sync_card.hpp`:
  - When paired: Mesh Sync Dashboard (status, server, client ID, E2EE passphrase configuration, dataset item counts, live sync logs).
  - When un-paired: Legacy Git Sync Dashboard.

### Phase 5: Server RFC Dispatch
- [ ] File formal RFC in `/Users/ignaciorodriguez/src/rouen-service/inbox/2026-10-04-rfc-mesh-persistent-registry-storage.md`.

### Phase 6: Build, Packaging & Verification
- [ ] Run full test suites.
- [ ] Compile target `rouen`.
- [ ] Deploy and ad-hoc sign to `$HOME/Applications/Rouen.app`.
