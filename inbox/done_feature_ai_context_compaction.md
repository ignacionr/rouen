# Feature Request: AI Context Compaction and Window Management

## Summary
Implement an intelligent context compaction and rolling window management mechanism for Rouen's AI conversational systems (Desktop `AIChat` card and `TelegramHost`). When conversation history exceeds a configurable threshold (measured in message turns or token budget), older conversation turns should be summarized into a concise context memory block rather than either growing unboundedly or being discarded abruptly.

---

## Motivation & Problem Statement

### 1. Unbounded Context Growth in Desktop AI Chat
In Rouen's desktop `AIChat` card (`src/cards/information/ai_chat.cpp`):
- Every user prompt and assistant response is appended to `chat_history_`.
- When constructing the payload for the LLM backend (`conversation_for_llm`), the system extracts **all** `"user"` and `"assistant"` messages since the start of the session (lines 1859–1868).
- The only eviction check in place is:
  ```cpp
  if (chat_history_.size() == chat_history_.max_size() - 2) {
      chat_history_.pop_front();
      message_cache_.pop_front();
  }
  ```
  Since `std::deque::max_size()` is on the order of billions of elements, this condition is never met in practice.
- The UI layer (`serialize_to_adaptive_card_json()`, lines 1169–1202) visually collapses earlier messages past 50 behind `"... X earlier conversation messages not shown ..."`, but this is purely cosmetic—the raw payload sent to the LLM still includes the full conversation history.

### 2. Hard Sliding Window in Telegram Host
In `src/hosts/telegram_host.cpp` (lines 1016–1025):
- The Telegram gateway limits conversation history by slicing the last 20 messages.
- Earlier messages are silently dropped. Any project background, instructions, or agreements established before the 20-message cutoff are lost, causing the agent to repeat questions or forget decisions.

### 3. Impact on Local and Cost-Metered Models
- **Local Models (MLX / Ollama / llama.cpp):** Local LLMs typically operate with 4k–32k context windows. Unbounded history causes prompt-processing latency spikes, heavy KV-cache memory pressure on Apple Silicon Unified Memory, and eventual `context_length_exceeded` / HTTP 400 failures.
- **Cloud Providers (OpenAI, Grok, Anthropic):** Continuously re-transmitting tens of thousands of tokens per turn increases per-request latency and financial cost.
- **Attention Drift:** As chat logs grow with voluminous code blocks, tool descriptions, and iterative queries, LLM instruction adherence and reasoning performance degrade.

---

## Proposed Architecture & Solution

### 1. Configuration & Persona Parameters
Add context management settings to `helpers::Persona` (`src/helpers/persona_manager.hpp`) and/or `helpers::LLMConfig`:
```json
{
  "context_compaction_strategy": "summarize", // Options: "none", "sliding_window", "summarize"
  "max_context_turns": 30,                   // Total user+assistant turns before triggering compaction
  "preserve_recent_turns": 10                // Number of most recent turns to preserve verbatim
}
```

### 2. Compaction Workflow
When a new user message arrives and `conversation_for_llm.size() > max_context_turns`:
1. **Partition History:** Split history into:
   - `older_turns`: Turns from index `0` to `total - preserve_recent_turns`.
   - `recent_turns`: The most recent `preserve_recent_turns` turns.
2. **Summarize Older Turns:**
   - Execute an asynchronous, non-blocking lightweight LLM summarization request (or run it inline with low temperature and high token efficiency):
     ```
     Summarize the following conversation history between User and Assistant.
     Extract and preserve:
     - Core user goals, constraints, and preferences.
     - Technical decisions made, file paths discussed, and established facts.
     - Pending tasks or unanswered questions.
     Keep the summary structured, factual, and under 300 words.
     ```
3. **Consolidate History:**
   - Replace `older_turns` in the LLM payload with a synthetic context memory block:
     ```markdown
     [Conversation Context & Earlier Memory Summary]:
     - User is working on refactoring Rouen's mesh networking.
     - Decided to use Ed25519 challenge-response for peer pairing.
     - Recent bug fix addressed registry explosion in done_bug_mesh_sync_conflict_loop.md.
     ```
   - Retain `recent_turns` verbatim so immediate dialogue flow and context references are seamless.

### 3. UI Indicator in Desktop Deck
In `AIChat`:
- Render an unobtrusive status pill or separator in the chat view:
  `🗜️ Context compacted: 24 earlier turns summarized into memory.`
- Clicking the pill can reveal or expand the generated summary.

---

## Technical Implementation Plan

1. **`src/helpers/persona_manager.hpp`**:
   - Add `max_context_turns` (default: 30) and `preserve_recent_turns` (default: 10) to `struct Persona`.
   - Serialize/deserialize these fields in JSON configurations.

2. **`src/cards/information/ai_chat.hpp` & `.cpp`**:
   - Implement `compact_conversation_history()` helper.
   - Maintain an optional `std::string context_summary_` field.
   - When message count exceeds `max_context_turns`, invoke summarization and store the resulting digest in `context_summary_`.
   - In `conversation_for_llm` assembly, inject `context_summary_` as a system/context turn followed by the tail of recent messages.

3. **`src/hosts/telegram_host.cpp`**:
   - Upgrade the hard sliding window in `run_ai_persona_completion` to adopt the same compaction summary pattern for session histories exceeding 20 messages.

4. **Testing (`tests/`)**:
   - Add unit tests verifying:
     - Context does not compact below `max_context_turns`.
     - When exceeded, `preserve_recent_turns` are preserved verbatim.
     - Summary block is properly prefixed into LLM messages.
     - Clear Chat resets both history and `context_summary_`.

---

## Value
- **Scalable Sessions:** Users can engage in day-long pair programming or debugging sessions without chat performance degradation or manual resets.
- **Local Model Viability:** Makes local MLX/Ollama models practical for extended workflows without blowing out context buffers.
- **Cost & Latency Optimization:** Reduces recurring prompt token payloads on external LLM APIs by up to 70–80% in mature sessions.
