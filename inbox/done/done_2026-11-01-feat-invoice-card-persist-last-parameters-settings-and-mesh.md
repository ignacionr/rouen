# Feature Request: Persist Last Used Provider & Customer Parameters in Invoice Card via Local Settings and Mesh Registry

**Date**: November 1, 2026  
**From**: Rouen Desktop Client UX & Mesh Engineering Team  
**To**: Rouen Architecture & Core Card Maintainers  
**Status**: Done  
**Resolution Date**: 2026-11-01
**Target Components**:
- Invoice Card Implementation: [`src/cards/productivity/invoice_card.hpp`](file:///Users/inz/src/rouen/src/cards/productivity/invoice_card.hpp), [`src/cards/productivity/invoice_card.cpp`](file:///Users/inz/src/rouen/src/cards/productivity/invoice_card.cpp)
- Configuration / Settings Service: [`src/helpers/config_service.cppm`](file:///Users/inz/src/rouen/src/helpers/config_service.cppm), [`src/cards/system/settings.hpp`](file:///Users/inz/src/rouen/src/cards/system/settings.hpp)
- Rouen Mesh Host & Registry: [`src/hosts/rouen_mesh_host.hpp`](file:///Users/inz/src/rouen/src/hosts/rouen_mesh_host.hpp), [`src/hosts/rouen_mesh_host.cpp`](file:///Users/inz/src/rouen/src/hosts/rouen_mesh_host.cpp)
- Unit Tests: [`tests/test_invoice_card.cpp`](file:///Users/inz/src/rouen/tests/test_invoice_card.cpp)

**File Location**: [`inbox/2026-11-01-feat-invoice-card-persist-last-parameters-settings-and-mesh.md`](file:///Users/inz/src/rouen/inbox/2026-11-01-feat-invoice-card-persist-last-parameters-settings-and-mesh.md)  

---

## 1. Executive Summary & Rationale

### 1.1 Overview
Currently, the Invoice Generator card ([`invoice_card`](file:///Users/inz/src/rouen/src/cards/productivity/invoice_card.hpp#L27-L109)) initializes every session with hardcoded default strings for the **Provider (Seller)** (`seller_name`, `seller_email`, `seller_address`, `seller_phone`, plus associated banking fields `bank_name`, `account_holder`, `swift_bic_type`, `swift_code`, `iban_account`) and **Customer (Client)** (`client_name`, `client_contact`, `client_address`), as well as metadata/retainer defaults.

Whenever a user selects a Provider or Customer from the Contacts picker, edits their details manually, or dispatches actions via `handle_action()` / MCP tools (`set_provider_from_contact`, `set_customer_from_contact`, `generate_invoice_pdf`), those parameters only live in memory for the lifetime of the card instance. Closing the card or restarting Rouen resets all fields back to the static defaults (`"Ignacio Rodriguez"`, `"Acme Corporation"`, etc.).

### 1.2 System Motivations
- **Zero-Friction Recurring Invoicing**: Users generating monthly or recurring invoices typically bill using the same Provider profile and most recent Customer profile. Persisting the last used parameters eliminates repetitive re-selection from Contacts or manual re-entry.
- **Local Settings Persistence**: Storing the last used parameters in Rouen's local configuration/settings (`ConfigService` / user data JSON or `.env`-backed settings) ensures offline resilience and immediate restoration on startup.
- **Cross-Node Roaming via Rouen Mesh Registry**: When connected to the Rouen Mesh (`rouen_mesh_host::instance().is_connected()`), synchronizing the last used invoice parameters to the Mesh Registry (`ephemeral = false`) allows a user roaming between nodes (e.g., MacMini and MacBook Air) or invoking remote invoice generation via Mesh/MCP to seamlessly share the most recent Provider and Customer state.

---

## 2. User Stories & Acceptance Criteria

### User Story 1: Automatic Save of Last Used Provider & Customer Parameters
> **As a user configuring an invoice in the Invoice Card**,  
> When I select a Provider or Customer from Contacts, edit their fields in the UI, or generate an invoice (via UI, Adaptive Card action, or MCP tool),  
> I want the card to automatically persist the last used Provider and Customer parameters locally in Settings and (if connected) publish them to the Rouen Mesh Registry.

### User Story 2: Automatic Restoration on Card Initialization
> **As a user opening the Invoice Card**,  
> I want the card to load the most recent Provider and Customer parameters from the Mesh Registry (when connected and available) or fall back to local Settings,  
> so that my last used billing parties and banking details are pre-populated immediately.

### User Story 3: Settings & Mesh Synchronization Controls
> **As a user operating across multiple Rouen nodes or offline**,  
> I want clear status visibility and deterministic fallback order (Mesh Registry if connected and newer -> Local Settings -> Default values),  
> without blocking the UI thread if the Mesh is disconnected.

---

## 3. Technical Architecture & Requirements

### 3.1 Persisted Parameter Payload
Define a serializable DTO (via Glaze `glz::json_t` / `glz::meta`) representing the last used invoice parameters, covering at minimum:
1. **Provider (Seller) & Banking Parameters**:
   - `seller_name` (`std::string`)
   - `seller_email` (`std::string`)
   - `seller_address` (`std::string`)
   - `seller_phone` (`std::string`)
   - `bank_name` (`std::string`)
   - `account_holder` (`std::string`)
   - `swift_bic_type` (`int`)
   - `swift_code` (`std::string`)
   - `iban_account` (`std::string`)
   - `foreign_statement` (`std::string`)
   - `selected_provider_contact_id` (`int64_t`, optional)
2. **Customer (Client) Parameters**:
   - `client_name` (`std::string`)
   - `client_contact` (`std::string`)
   - `client_address` (`std::string`)
   - `selected_customer_contact_id` (`int64_t`, optional)
3. **Optional Invoice Defaults & Timestamp**:
   - `payment_terms` (`std::string`)
   - `currency` (`std::string`)
   - `retainer_amount` (`double`)
   - `updated_at_ms` (`uint64_t` epoch timestamp for conflict resolution between local settings and mesh registry)

### 3.2 Dual-Layer Persistence Strategy

1. **Local Settings Layer (`ConfigService` / User Data Config)**:
   - Register/store settings keys under `ConfigService` (e.g., `INVOICE_LAST_PROVIDER_JSON`, `INVOICE_LAST_CUSTOMER_JSON`, or unified `INVOICE_LAST_PARAMS_JSON` in `Category::GENERAL`, or a dedicated user-data settings file alongside `.env`).
   - Persist changes via `ConfigService::instance()->set_env_value(..., /*persist_to_env_file=*/true)` or structured JSON settings helper so they survive application restarts.

2. **Mesh Registry Layer (`rouen_mesh_host`)**:
   - Use well-defined, non-ephemeral Mesh Registry keys:
     - `invoice/settings/last_params` (or `invoice/last_provider` and `invoice/last_customer`) with `ephemeral = false` via `rouen_mesh_host::instance().set_registry_value(key, json_payload, /*ephemeral=*/false)`.
   - Only publish/query the Mesh Registry when `rouen_mesh_host::instance().is_connected()` is `true`.

### 3.3 Save & Load Lifecycle Hooks in `invoice_card`
- **Load (`load_last_parameters()`)**:
  - Called at the end of `invoice_card::invoice_card()`.
  - Loads local settings first as the baseline.
  - If `rouen_mesh_host::instance().is_connected()` is `true`, checks `get_registry_value("invoice/settings/last_params")` (or prefix `invoice/settings/`). If present and valid JSON (with `updated_at_ms` >= local timestamp), applies the mesh registry values and syncs them back to local settings.
  - Reconciles `selected_provider_combo_idx_` and `selected_customer_combo_idx_` against `cached_contacts_` if matching contact IDs or names exist.
- **Save (`save_last_parameters()`)**:
  - Triggered when:
    - `apply_contact_as_provider()` or `apply_contact_as_customer()` is called.
    - Provider or Customer input fields are modified in `invoice_card::render()`.
    - `handle_action()` updates provider/customer parameters or generates a PDF.
  - Writes updated JSON state to local settings and, if `rouen_mesh_host::instance().is_connected()`, calls `set_registry_value("invoice/settings/last_params", json_payload, /*ephemeral=*/false)`.

---

## 4. Verification & Testing Plan

1. **Unit Tests (`tests/test_invoice_card.cpp`)**:
   - **Local Settings Persistence Test**: Instantiate `invoice_card`, apply a provider and customer (via `apply_contact_as_provider` / `apply_contact_as_customer` or `handle_action`), destroy the card, instantiate a new `invoice_card`, and verify that the provider (`seller_name`, `seller_email`, `seller_address`, `seller_phone`) and customer (`client_name`, `client_contact`, `client_address`) fields are restored from local settings.
   - **Mesh Registry Persistence Test**: When `rouen_mesh_host` is connected (or via registry mock/state in `rouen_mesh_host::instance()`), verify that updating provider/customer parameters writes the expected non-ephemeral entry to the mesh registry (`get_registry_value("invoice/settings/last_params")`) and that a newly constructed `invoice_card` restores parameters from the mesh registry entry.
   - **Offline Fallback Test**: Verify that when `rouen_mesh_host::instance().is_connected()` is `false`, saving and loading still succeeds cleanly via local settings without errors.
2. **Build & Deployment Verification**:
   - Compile test and application targets with `-j2` concurrency limit (`nix develop --command cmake --build build --target rouen -j2`).
   - Deploy and sign `Rouen.app` bundle per repository rules.


---

## Resolution & Outcome Summary

Implemented local settings and mesh registry persistence for invoice card provider/customer parameters. Created invoice_card_params DTO with glz meta. Added load_last_parameters and save_last_parameters lifecycle hooks with automated synchronization across Rouen Mesh nodes. Verified with build and deployment.
