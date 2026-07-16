# Rouen

[![CI](https://github.com/ignacionr/rouen/actions/workflows/ci-nix.yml/badge.svg)](https://github.com/ignacionr/rouen/actions/workflows/ci-nix.yml)

Rouen is a modern, card-based productivity workspace designed to streamline developer tasks, personal scheduling, system monitoring, and communication interfaces. Every feature is packaged as a draggable, resizable, and focusable "card" that can be customized to fit your layout.

![Rouen Dashboard](img/screenshot.png)

---

## About

The name **Rouen** is inspired by the Rouen pattern, a historic French playing card suit system. This naming reflects the application's card-based UI design, where each feature is presented as an interactive card that can be arranged and managed in your workspace.

---

## Documentation

To make the codebase easy to navigate, the documentation has been split into modular guides:

* 📦 **[Installation Guide](docs/INSTALL.md)**: Steps to install pre-built binaries (MSI, DMG, TAR.GZ) and automated installation scripts for Windows, macOS, and Linux.
* ⚙️ **[User & Usage Guide](docs/USAGE.md)**: Operating guidelines, dynamic card URIs, keyboard shortcuts, environment variables configuration list, and troubleshooting.
* 🏛️ **[Architecture Guide](docs/ARCHITECTURE.md)**: Design overview, the Card system, core helper services, and architecture diagrams.
* 🛠️ **[Development & Contributing Guide](docs/DEVELOPMENT.md)**: Nix environment setup, compiling from source, building/running tests (Google Test), and guidelines for creating new cards or helpers.
* 🔄 **[Git Synchronization Guide](docs/GIT_SYNC.md)**: Native Git-based synchronization setup for multi-device syncing of notes, travel plans, RSS subscriptions, objectives, and layouts.
* 🧠 **[AI & MCP Integration Guide](docs/AI.md)**: Detailed overview of AI capabilities (chat, command translation, analysis) and Model Context Protocol (MCP) tool integration.
* 📰 **[RSS Reader Guide](docs/RSS.md)**: Consolidated documentation for the RSS reader subsystem, including media extraction pipelines and detailed UI card specifications.
* 📇 **[Adaptive Cards Integration Plan](docs/adaptive_cards_plan.md)**: Step-by-step architectural design and execution plan to bring Adaptive Cards and Templating to Rouen.
* 🧩 **[Adaptive Cards (Official GitHub)](https://github.com/microsoft/AdaptiveCards)**: Upstream schema, samples, and renderer ecosystem.

---

## Key Features

* **Development Tools**: Visual Git repository browser, GitHub workflow/CI monitoring, interactive directory explorer, syntax-highlighted code editor, CMake build integration, environment variable manager, and Jira/Trello boards.
* **Productivity & Planning**: Pomodoro timer, configurable Alarms, Trello columns integration, travel planner, Google Calendar sync, a Notifications center with spoken/silent control, and personal Markdown notes with wiki-style linking.
* **Information & Media**: IMAP/SMTP email client, local weather tracking, RSS reader with media extraction (video/audio) and AI feed discovery, AI chat assistant (Grok, OpenAI, Groq, custom), and internet radio player with MPV.
* **Financial Services**: Bybit trading account assets viewer, and type conversion calculators.
* **Games**: Chess replay analysis with AI strategic commentary.

---

## License

Rouen is open-source software licensed under the [MIT License](LICENSE).
