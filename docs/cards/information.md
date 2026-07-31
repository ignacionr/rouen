# Rouen Card Documentation: Information & Intelligence

> [!NOTE]
> Technical overview, REST API creation URI, and live UI snapshots for all supported **Information & Intelligence** cards running on Rouen.

## Overview Table

| Card Title | URI Schema | Description |
| :--- | :--- | :--- |
| **AI Assistant Chat Card** | `uri: "ai_chat"` | Conversational AI card with LLM tool calling, memory context, and interactive card generation. |
| **Calendar Card** | `uri: "calendar"` | Schedule viewer supporting iCal feeds, WebDAV calendar sync, and event reminders. |
| **Weather Forecast Card** | `uri: "weather"` | Global weather forecast display with hourly temperature graphs, precipitation probability, and wind metrics. |
| **Wikipedia Card** | `uri: "wikipedia"` | Encyclopedia article browser featuring clean typography, table of contents, and cross-reference links. |
| **Markdown Notes Card** | `uri: "notes"` | Rich Markdown notebook supporting live side-by-side rendering, task lists, and syntax highlighted code blocks. |
| **Contacts Directory Card** | `uri: "contacts"` | Address book and contact management card supporting gravatar icons and search. |
| **Adaptive Card Renderer** | `uri: "adaptive-card"` | Microsoft Adaptive Cards JSON layout engine rendering dynamic schema-driven user interface components. |
| **Number Series Visualization Card** | `uri: "number-series"` | Data series plotter rendering bar charts, line graphs, and comparative metric visualizations. |
| **Travel Planner Card** | `uri: "travel"` | Flight, hotel, and itinerary management card for organizing travel bookings. |
| **RSS News Aggregator Card** | `uri: "rss"` | Newsfeed reader aggregating RSS/Atom feeds with smart filtering, grayscale media preview, and article extraction. |

---

## AI Assistant Chat Card

- **URI Schema**: `ai_chat`
- **Category**: Information & Intelligence

### Description & Features
Conversational AI card with LLM tool calling, memory context, and interactive card generation.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"ai_chat"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"ai_chat"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_ai_chat.png"}'
```

### Live UI Snapshot (Captured via REST API)

![AI Assistant Chat Card snapshot running on Rouen](images/card_ai_chat.png)

---

## Calendar Card

- **URI Schema**: `calendar`
- **Category**: Information & Intelligence

### Description & Features
Schedule viewer supporting iCal feeds, WebDAV calendar sync, and event reminders.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"calendar"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"calendar"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_calendar.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Calendar Card snapshot running on Rouen](images/card_calendar.png)

---

## Weather Forecast Card

- **URI Schema**: `weather`
- **Category**: Information & Intelligence

### Description & Features
Global weather forecast display with hourly temperature graphs, precipitation probability, and wind metrics.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"weather"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"weather"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_weather.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Weather Forecast Card snapshot running on Rouen](images/card_weather.png)

---

## Wikipedia Card

- **URI Schema**: `wikipedia`
- **Category**: Information & Intelligence

### Description & Features
Encyclopedia article browser featuring clean typography, table of contents, and cross-reference links.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"wikipedia"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"wikipedia"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_wikipedia.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Wikipedia Card snapshot running on Rouen](images/card_wikipedia.png)

---

## Markdown Notes Card

- **URI Schema**: `notes`
- **Category**: Information & Intelligence

### Description & Features
Rich Markdown notebook supporting live side-by-side rendering, task lists, and syntax highlighted code blocks.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"notes"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"notes"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_notes.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Markdown Notes Card snapshot running on Rouen](images/card_notes.png)

---

## Contacts Directory Card

- **URI Schema**: `contacts`
- **Category**: Information & Intelligence

### Description & Features
Address book and contact management card supporting gravatar icons and search.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"contacts"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"contacts"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_contacts.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Contacts Directory Card snapshot running on Rouen](images/card_contacts.png)

---

## Adaptive Card Renderer

- **URI Schema**: `adaptive-card`
- **Category**: Information & Intelligence

### Description & Features
Microsoft Adaptive Cards JSON layout engine rendering dynamic schema-driven user interface components.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"adaptive-card"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"adaptive-card"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_adaptive-card.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Adaptive Card Renderer snapshot running on Rouen](images/card_adaptive_card.png)

---

## Number Series Visualization Card

- **URI Schema**: `number-series`
- **Category**: Information & Intelligence

### Description & Features
Data series plotter rendering bar charts, line graphs, and comparative metric visualizations.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"number-series"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"number-series"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_number-series.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Number Series Visualization Card snapshot running on Rouen](images/card_number_series.png)

---

## Travel Planner Card

- **URI Schema**: `travel`
- **Category**: Information & Intelligence

### Description & Features
Flight, hotel, and itinerary management card for organizing travel bookings.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"travel"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"travel"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_travel.png"}'
```

### Live UI Snapshot (Captured via REST API)

![Travel Planner Card snapshot running on Rouen](images/card_travel.png)

---

## RSS News Aggregator Card

- **URI Schema**: `rss`
- **Category**: Information & Intelligence

### Description & Features
Newsfeed reader aggregating RSS/Atom feeds with smart filtering, grayscale media preview, and article extraction.

### REST API Interaction
```bash
# Create card
curl -X POST http://127.0.0.1:8081/api/cards -H "Content-Type: application/json" -d '{"uri":"rss"}'

# Focus card
curl -X POST http://127.0.0.1:8081/api/cards/focus -H "Content-Type: application/json" -d '{"uri":"rss"}'

# Capture selected card snapshot
curl -X POST http://127.0.0.1:8081/api/screenshot -H "Content-Type: application/json" -d '{"target":"selected","filename":"/tmp/snapshot_rss.png"}'
```

### Live UI Snapshot (Captured via REST API)

![RSS News Aggregator Card snapshot running on Rouen](images/card_rss.png)

---

