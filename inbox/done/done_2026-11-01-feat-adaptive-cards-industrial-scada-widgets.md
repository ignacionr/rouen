# Feature Request: Industrial SCADA & Oilfield Widgets (`Rouen.SeriesPlot`, `Rouen.Gauge`, `Rouen.XYPlot`, `Rouen.StatusBadge`) Leveraging Rouen's Existing `number_series` Graph Engine

- **Date**: 2026-11-01
- **Type**: `feat`
- **Target Sector**: Energy & Utilities / Oil & Gas Production & Drilling HMIs
- **Status**: Open

---

## 1. Problem Statement & Existing Rouen Capabilities to Leverage

Rouen **already** includes a rich data-graphing and time-series subsystem:
1. **`number_series_card::draw_series_plot()`** ([`src/cards/information/number_series.hpp`](../src/cards/information/number_series.hpp#L880-L1119)): Renders shaded-area line charts (`PathFillConvex`), multi-color gradient bar charts (`AddRectFilledMultiColor`), auto-scaled Y-axis gridlines, interactive mouse-hover hairlines, and floating value tooltips.
2. **`models::series::series_repository` & `series_record`** ([`src/models/series/series_repository.hpp`](../src/models/series/series_repository.hpp#L25-L62)): SQLite-backed persistence (`series.db`) for named series (`name`, `title`, `unit`, `points: [{label, value}]`, `is_bar_chart`, `color_index`), plus inline JSON ingestion (`number-series:{"name":...}`) and one-click CSV export (`export_csv`).
3. **Live Video Broadcast Overlay (`render_video_ui()`)** ([`src/cards/information/number_series.hpp`](../src/cards/information/number_series.hpp#L416-L507)): Overlays animated series charts onto Rouen's camera/video stream (`video_feed_host`)—ideal for combining a live rig-floor, flare-stack, or substation thermal camera feed with live sensor telemetry.
4. **Strategic KPI Formatting (`kpi_card`)** ([`src/cards/productivity/kpi_card.hpp`](../src/cards/productivity/kpi_card.hpp#L27-L75)): Target vs. current percentage calculation and health status badges (`Healthy` / `Attention`).

### What Is Currently Disconnected
- **No Graph in Adaptive Card View or Over the Mesh**: When [`number_series_card::get_adaptive_card_json()`](../src/cards/information/number_series.hpp#L510-L724) serializes a series for `Adaptive Card View` or remote viewing over [`mesh_card_proxy`](../src/cards/interface/mesh_card_proxy.hpp), it currently falls back to a text-only `FactSet` (Current, Min, Peak, Sample Count) because [`helpers::adaptive_cards::renderer`](../src/helpers/adaptive_cards/renderer.hpp) cannot yet invoke `draw_series_plot()`.
- **No Bridge Between `adaptive-process` and `series_repository`**: An external sensor script running in [`adaptive_process_card`](../src/cards/information/adaptive_process_card.hpp) cannot embed a `draw_series_plot` canvas inline, nor can it automatically feed points into `series_repository` (`series.db`) to drive `number-series:` charts and camera video overlays.

---

## 2. Proposed Architecture & Design

### A. Extract `draw_series_plot` into a Reusable Helper (`src/helpers/series_plot_renderer.hpp`)
Refactor [`number_series_card::draw_series_plot()`](../src/cards/information/number_series.hpp#L880-L1119) into a standalone, stateless ImGui helper `rouen::helpers::series_plot_renderer::draw(...)` that accepts:
- `std::span<const models::series::data_point> points`
- `plot_config` (`unit`, `is_bar_chart`, `active_color`, `height`, `compact_sparkline_mode`, and optional industrial alarm limit lines: `low_limit`, `high_limit`, `target_value`).
- Both `number_series_card` and `helpers::adaptive_cards::renderer` will call this single shared plotting engine.

### B. Add `Rouen.SeriesPlot` (and Compact `Rouen.Sparkline`) to Adaptive Cards
Extend [`src/helpers/adaptive_cards/parser.hpp`](../src/helpers/adaptive_cards/parser.hpp) and [`renderer.hpp`](../src/helpers/adaptive_cards/renderer.hpp) to support `"type": "Rouen.SeriesPlot"`, using the exact same `data_point` (`label`, `value`) schema as `series_record`:

```json
{
  "type": "Rouen.SeriesPlot",
  "title": "Tubing Pressure (Last 15m)",
  "seriesName": "pad14-well3h-tbg-psi",
  "persistToRepository": true,
  "unit": "psi",
  "isBarChart": false,
  "height": 140,
  "lowLimit": 250.0,
  "highLimit": 750.0,
  "points": [
    { "label": "21:10", "value": 478.0 },
    { "label": "21:12", "value": 481.5 },
    { "label": "21:14", "value": 485.0 }
  ]
}
```
- **Dual Data Source (Inline or `series_repository` Lookup)**:
  - If `"points"` is provided and `"persistToRepository": true`, Rouen automatically upserts the series into `models::series::series_repository` (`series.db`). This means any `adaptive-process` sensor script automatically populates `number-series:<seriesName>` for historical exploration, CSV export, and **live Camera Video Broadcast Overlays**!
  - If `"points"` is omitted and `"seriesName"` is set, the renderer loads the points from `series_repository::get_series_by_name(seriesName)`.
- **Upgrade `number_series_card::get_adaptive_card_json()`**:
  - Emit a `"type": "Rouen.SeriesPlot"` element in [`number_series_card::get_adaptive_card_json()`](../src/cards/information/number_series.hpp#L510-L724) so `Adaptive Card View` and remote `[edge-node]/number-series:<name>` cards over the mesh render the full interactive line/bar chart!

### C. Add `Rouen.Gauge`, `Rouen.XYPlot` (Dynacard), and `Rouen.StatusBadge`
Complement `Rouen.SeriesPlot` with three lightweight industrial primitives in `series_plot_renderer.hpp` / `renderer.hpp`:

1. **`Rouen.Gauge` (Process Bar/Arc Gauge with ISA-101 Threshold Bands + KPI Target)**:
   ```json
   {
     "type": "Rouen.Gauge",
     "label": "Casing Pressure",
     "value": 620.0,
     "target": 550.0,
     "unit": "psi",
     "min": 0.0,
     "max": 1000.0,
     "thresholds": { "lowLow": 50, "low": 100, "high": 750, "highHigh": 850 }
   }
   ```
   Combines `kpi_card`'s target-delta formatting with visual Low-Low / Low / Normal / High / High-High color zones.

2. **`Rouen.XYPlot` (2D Closed-Loop / XY Curve for Rod Pump Dynacards & Compressor Maps)**:
   ```json
   {
     "type": "Rouen.XYPlot",
     "title": "Rod Pump Surface vs. Downhole Dynacard",
     "xLabel": "Position (in)",
     "yLabel": "Load (lbs)",
     "points": [[0, 12000], [40, 22000], [120, 21500], [80, 9500], [0, 12000]]
   }
   ```
   Reuses the canvas, gridline, and hover-tooltip logic from `series_plot_renderer.hpp` for 2D `[x, y]` coordinate pairs.

3. **`Rouen.StatusBadge` (High-Contrast Operational State Pill)**:
   ```json
   {
     "type": "Rouen.StatusBadge",
     "label": "Main SSV Valve",
     "text": "FLOWING (SSV OPEN)",
     "severity": "normal"
   }
   ```

---

## 3. Affected Files

- `src/helpers/series_plot_renderer.hpp` (new — extracted from `number_series.hpp`)
- [`src/cards/information/number_series.hpp`](../src/cards/information/number_series.hpp)
- [`src/models/series/series_repository.hpp`](../src/models/series/series_repository.hpp)
- [`src/helpers/adaptive_cards/parser.hpp`](../src/helpers/adaptive_cards/parser.hpp)
- [`src/helpers/adaptive_cards/renderer.hpp`](../src/helpers/adaptive_cards/renderer.hpp)
- [`docs/ADAPTIVE_PROCESS_CARDS.md`](../docs/ADAPTIVE_PROCESS_CARDS.md)

---

## 4. Verification & Unit Test Plan

1. Add unit tests in `tests/` verifying that `parser::parse()` deserializes `Rouen.SeriesPlot`, `Rouen.Gauge`, `Rouen.XYPlot`, and `Rouen.StatusBadge`.
2. Verify that `number_series_card::get_adaptive_card_json()` includes the `Rouen.SeriesPlot` element with the active series points and metadata.
3. Verify that an `adaptive-process` card emitting `Rouen.SeriesPlot` with `"persistToRepository": true` updates `series_repository` (`series.db`) so `number-series:<seriesName>` immediately reflects the live sensor data.
