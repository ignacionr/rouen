# Bug: RSS Gallery Text Overflow Enables Horizontal Scrolling and Triggers Runaway Right Blank Space

## Summary

The RSS Gallery card (`rss`, URI: `rss`) suffers from a coordinate-space and layout feedback bug in its scrollable feed grid (`FeedsScrollArea`):

1. **Text Overflow & Initial Horizontal Scroll Activation**: Feed tile titles in [`rss::render_feed_list`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L1088-L1092) call `ImGui::PushTextWrapPos(start_pos.x + card_width - 6.0f * dpi_scale)` using **screen-space** coordinates (`ImGui::GetCursorScreenPos()`) instead of **window-local** coordinates (`ImGui::GetCursorPos()`). Because Dear ImGui's `CalcWrapWidthForPos` adds `window->Pos.x - window->Scroll.x` internally to convert local coordinates to screen coordinates, `window->Pos.x` is added twice. Long feed titles therefore fail to wrap at the right edge of the `180px` tile, overflowing beyond the rightmost column and inflating the child window's `CursorMaxPos.x` (`ContentSize.x`). Additionally, [`glass_top_bar::render`](file:///Users/inz/src/rouen/src/helpers/glass_top_bar.cpp#L44) inserts a top spacer `ImGui::Dummy(ImVec2(w, last_top_h))` using the outer width `w`, which exceeds the inner content width by `ScrollbarSize` whenever the vertical scrollbar is active.
2. **Runaway Right-Side Blank Area on Horizontal Scroll**: Once `ScrollMax.x > 0` allows horizontal scrolling (e.g., via a trackpad two-finger gesture), [`rss::render_feed_list`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L989-L990) and the deep-search article list ([`rss.cpp:783-823`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L783-L823)) compute layout width from `ImGui::GetContentRegionAvail().x`. In Dear ImGui, `GetContentRegionAvail().x` equals `window->ContentRegionRect.Max.x - window->DC.CursorPos.x`. Because `window->DC.CursorPos.x` starts at `window->Pos.x - window->Scroll.x`, scrolling right by `Scroll.x` increases `GetContentRegionAvail().x` to `VisibleWidth + Scroll.x`. As the user scrolls right, `avail_width` grows continuously, increasing the computed column count (`cols`) and pushing content and `CursorMaxPos.x` progressively further right into an ever-widening blank area.

---

## Root Cause Analysis

### 1. Screen-Space Coordinate Passed to `ImGui::PushTextWrapPos` (`src/cards/information/rss.cpp`)

In [`rss::render_feed_list`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L1016-L1109):

```cpp
ImVec2 const start_pos = ImGui::GetCursorScreenPos();
...
// 2. Draw Title with proper spacing
ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 6.0f * dpi_scale, start_pos.y + 230.0f * dpi_scale));
const ImVec4 text_color = is_emphasized ? colors[0] : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImGui::PushTextWrapPos(start_pos.x + card_width - 6.0f * dpi_scale);
ImGui::TextColored(text_color, "%s", title.c_str());
ImGui::PopTextWrapPos();
```

- `ImGui::PushTextWrapPos(float wrap_local_pos_x)` expects `wrap_local_pos_x` in **window-local space** (relative to `window->Pos.x - window->Scroll.x`, matching `ImGui::GetCursorPos().x`).
- Inside Dear ImGui's `CalcWrapWidthForPos(pos, wrap_pos_x)`:
  ```cpp
  wrap_pos_x += window->Pos.x - window->Scroll.x; // wrap_pos_x is provided in window local space
  return ImMax(wrap_pos_x - pos.x, 1.0f);
  ```
- Substituting `wrap_pos_x = start_pos.x + card_width - 6.0f` and `pos.x = start_pos.x + 6.0f` yields an effective wrap width of:
  $$\text{wrap\_width} = (\text{card\_width} - 12) + \text{window->Pos.x} - \text{window->Scroll.x}$$
- Whenever the RSS Gallery card is positioned at `window->Pos.x > 0` on the deck, feed titles are allowed to extend up to `window->Pos.x` pixels past the right edge of each `180px` tile.
- Furthermore, the tile is wrapped in `ImGui::BeginGroup()` / `ImGui::EndGroup()` without clipping (`ImGui::PushClipRect`) or line-count truncation. Even though lines 1120–1121 reset the cursor and emit `ImGui::Dummy(ImVec2(card_width, card_height))`, `ImGui::TextColored` and `ImGui::EndGroup()` have already expanded `window->DC.CursorMaxPos.x` (and vertically overflowed the `60px` footer if a title wraps to multiple lines).

### 2. Positive Feedback Loop Between `Scroll.x` and `ImGui::GetContentRegionAvail().x` (`src/cards/information/rss.cpp`)

In [`rss::render_feed_list`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L989-L990):

```cpp
float const avail_width = ImGui::GetContentRegionAvail().x;
int const cols = std::max(1, static_cast<int>(avail_width / (card_width + spacing)));
```

And in the deep-search article results ([`rss::render`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L783-L823)):

```cpp
float const avail_width = ImGui::GetContentRegionAvail().x;
if (has_item_image) {
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(avail_width - 130.0f);
} else {
    ImGui::PushTextWrapPos(avail_width);
}
...
if (ImGui::Selectable(item.title.c_str(), false, 0, ImVec2(has_item_image ? avail_width - 130.0f : avail_width, 0))) { ... }
...
ImGui::SameLine(avail_width - 120.0f);
```

- In Dear ImGui, `ImGui::GetContentRegionAvail().x` is computed as `window->ContentRegionRect.Max.x - window->DC.CursorPos.x`.
- At the start of a row inside a child window scrolled horizontally by `Scroll.x`, `window->DC.CursorPos.x` is `window->Pos.x - window->Scroll.x`, while `window->ContentRegionRect.Max.x` remains anchored to the visible viewport (`window->Pos.x + VisibleWidth`).
- Consequently, `ImGui::GetContentRegionAvail().x` evaluates to `VisibleWidth + window->Scroll.x`.
- As the user scrolls right (`Scroll.x > 0`):
  1. `avail_width` grows by `+ Scroll.x`.
  2. Once `VisibleWidth + Scroll.x` crosses the next multiple of `card_width + spacing` (`192px`), `cols` increments (`4 -> 5 -> 6 -> ...`), placing tiles into additional columns further to the right.
  3. Those new columns (plus their overflowing title wrap width) push `window->DC.CursorMaxPos.x` (`ScrollMax.x`) even further right, creating a runaway feedback loop where scrolling right continuously expands a blank area on the right side.

### 3. Top Glass Bar Spacer Width & Unconstrained Horizontal Scroll (`src/helpers/glass_top_bar.cpp`)

In [`glass_top_bar::render`](file:///Users/inz/src/rouen/src/helpers/glass_top_bar.cpp#L42-L48):

```cpp
if (ImGui::BeginChild(content_child_id.c_str(), ImVec2(w, h), false, ImGuiWindowFlags_NavFlattened)) {
    if (last_top_h > 0.0f) {
        ImGui::Dummy(ImVec2(w, last_top_h));
    }
    render_content_fn();
}
```

- When the vertical scrollbar is active inside `content_child_id`, the usable inner width is `w - ImGui::GetStyle().ScrollbarSize`.
- Calling `ImGui::Dummy(ImVec2(w, last_top_h))` submits an item of width `w`, which exceeds the inner content width by `ScrollbarSize` and makes `ScrollMax.x > 0` even when all feed tiles fit within the viewport.

---

## Steps to Reproduce

1. Open the **RSS Reader** (`rss`) card on the deck (especially when positioned with `window->Pos.x > 0`).
2. Ensure there are subscribed feeds with moderately long titles (wider than `168px`).
3. Scroll the feed gallery using a trackpad or mouse wheel with any slight horizontal scroll component (`io.MouseWheelH`).
4. Continue scrolling to the right.

### Expected Behavior
- Feed titles wrap cleanly (and clip/truncate) within the `180x290` feed tile boundaries without overflowing horizontally or vertically.
- The `FeedsScrollArea` child window only scrolls vertically; horizontal scrolling (`Scroll.x` / `ScrollMax.x`) is never enabled.
- Scrolling never alters the column count (`cols`) or creates blank horizontal space on the right side of the gallery.

### Actual Behavior
- Long feed titles overflow past the right edge of their tiles and expand the child window's horizontal content size.
- Horizontal scrolling becomes active in `FeedsScrollArea`.
- Scrolling right increases `GetContentRegionAvail().x` (`VisibleWidth + Scroll.x`), which increases `cols` and stretches `CursorMaxPos.x`, producing an increasingly wide blank area on the right side.

---

## Proposed Fix

### 1. Fix Coordinate Space, Title Truncation, and Tile Clipping in `rss::render_feed_list` (`src/cards/information/rss.cpp`)

- Compute `avail_width` from the non-scrolling inner viewport width (or lock `ImGui::SetScrollX(0.0f)` at the top of the scroll container) so `cols` depends solely on the visible container width:
  ```cpp
  ImGui::SetScrollX(0.0f);
  float const avail_width = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
  int const cols = std::max(1, static_cast<int>((avail_width + spacing) / (card_width + spacing)));
  ```
- Clip each tile to `[start_pos, end_pos]` with `ImGui::PushClipRect(start_pos, end_pos, true)` / `ImGui::PopClipRect()`.
- Use **window-local** coordinates for `ImGui::PushTextWrapPos` (or single/two-line `truncate_text` within `card_width - 12.0f * dpi_scale`) so the title cannot overflow horizontally or push the freshness row below the bottom of the tile:
  ```cpp
  ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 6.0f * dpi_scale, start_pos.y + 230.0f * dpi_scale));
  const ImVec4 text_color = is_emphasized ? colors[0] : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  float const text_max_w = card_width - 12.0f * dpi_scale;
  std::string const display_title = truncate_text(title, text_max_w * 2.0f - 16.0f * dpi_scale);
  ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + text_max_w);
  ImGui::TextColored(text_color, "%s", display_title.c_str());
  ImGui::PopTextWrapPos();
  ```
- Pin the freshness indicator and Play button row to a fixed Y offset near the bottom of the card (`start_pos.y + card_height - 26.0f * dpi_scale`) so multi-line titles never push the freshness row outside the tile.

### 2. Fix Deep-Search Article List Wrap Positions (`src/cards/information/rss.cpp`)

In [`rss::render`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp#L783-L823):
- Use `ImGui::GetCursorPos().x + avail_width - 130.0f` (and `ImGui::GetCursorPos().x + avail_width`) for `ImGui::PushTextWrapPos(...)`, and ensure `avail_width` is bounded by the visible inner width.

### 3. Prevent Horizontal Overflow in `glass_top_bar::render` (`src/helpers/glass_top_bar.cpp`)

In [`glass_top_bar::render`](file:///Users/inz/src/rouen/src/helpers/glass_top_bar.cpp#L42-L48):
- Replace `ImGui::Dummy(ImVec2(w, last_top_h))` with `ImGui::Dummy(ImVec2(0.0f, last_top_h))` so the top spacer only advances Y without exceeding `w - ScrollbarSize` when the vertical scrollbar is present.
- Clamp horizontal scroll inside `content_child_id` (`ImGui::SetScrollX(0.0f)`) so 2D trackpad scrolling only scrolls vertically.

---

## Affected Files

- [`src/cards/information/rss.cpp`](file:///Users/inz/src/rouen/src/cards/information/rss.cpp) — Fix `ImGui::PushTextWrapPos` coordinate space, tile clipping/truncation, and `avail_width` calculation in `rss::render_feed_list` and deep-search item rendering.
- [`src/helpers/glass_top_bar.cpp`](file:///Users/inz/src/rouen/src/helpers/glass_top_bar.cpp) — Change top spacer `ImGui::Dummy(ImVec2(w, last_top_h))` to `ImVec2(0.0f, last_top_h)` and prevent unintended horizontal scrolling in the content child window.
- [`tests/`](file:///Users/inz/src/rouen/tests) — Add unit/UI layout regression test verifying that RSS gallery rendering with long feed titles does not expand `ScrollMax.x` or enable horizontal scrolling.

---

## Acceptance Criteria

- [ ] Feed titles in the RSS Gallery wrap and clip cleanly within each `180x290` tile regardless of the RSS card's screen X position (`window->Pos.x`).
- [ ] `FeedsScrollArea` never enables horizontal scrolling (`ScrollMax.x == 0`, `Scroll.x == 0`).
- [ ] Trackpad / mouse wheel horizontal scroll input over the RSS Gallery does not shift content horizontally or create blank space on the right side.
- [ ] Multi-line feed titles do not push the freshness indicator (`●`) or Play button (`▶`) outside the bottom of the feed tile.


---

## Resolution & Outcome Summary

Fixed feed list text wrapping coordinate space bug by switching from screen-space to window-local coordinates, locking ScrollX to 0.0f in FeedsScrollArea, fixing glass top bar dummy spacer sizing, and validating fix with comprehensive unit tests in test_rss_gallery_layout.cpp.
