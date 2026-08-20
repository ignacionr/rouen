# Adaptive Process Cards

The `adaptive-process` schema turns any executable into a live Rouen card,
with no compiling against Rouen at all: the process just prints Adaptive
Card JSON to its stdout, and reads submitted form data from its stdin.

This is the third of Rouen's extensibility mechanisms, and the lightest
one:

| Mechanism | What you write | Toolchain match required? |
|---|---|---|
| DLL plugin, imperative | C++, `plugin_card` (ImGui) | Yes - see [docs/PLUGINS.md](PLUGINS.md) |
| DLL plugin, declarative | C++, `adaptive_card_plugin` (JSON) | Yes (compiler/CRT only, no ImGui) |
| **Adaptive process card** | **Any language, print/read stdio** | **No** |

Use this when the "plugin" is really just "a script that computes some
data" - a health check, a status dashboard for something you're running, a
small form-driven tool - and you don't want to set up a C++ build for it.

---

## The protocol

The card's URI locator is the full command line to launch, e.g.:

```
adaptive-process:python C:/scripts/weather_card.py --city Rouen
```

Once launched, with the process's stdin, stdout, and stderr all piped to
Rouen:

1. **stdout, one line at a time**: each complete line the process writes
   is treated as one compact Adaptive Card JSON document (no embedded
   literal newlines - print with a compact/minified JSON encoder) and
   replaces the card's current content. The process should print its
   first card immediately after starting.
2. **stdin, one line at a time**: when the user activates `Action.Submit`
   or `Action.Execute` on the currently shown card, Rouen writes the
   resulting JSON payload to the process's stdin as one line, matching
   the Adaptive Cards spec: a flat object of every input's current value
   keyed by its `id`, merged with the action's own `data` property if it
   is an object (e.g. an `Action.Submit` with `"data":{"action":"guess"}`
   and an `Input.Number` `id="guess"` set to 40 sends
   `{"action":"guess","guess":"40"}`). `Action.Execute` wraps that same
   merged object under `data`, alongside `verb`:
   `{"verb":"...","data":{...}}`. Read a line, compute, print a new card.
3. **`Action.OpenUrl`** is handled by Rouen only (opened with the OS's
   default handler) and is never sent to the process.
4. **stderr** is captured and shown in a collapsible "Process stderr"
   section on the card, for debugging - it is not part of the protocol,
   just diagnostics.

There is no framing beyond newlines: no length prefixes, no sentinel
markers, no request IDs. Keep each card and each submit payload to one
line.

### Minimal example (Python)

```python
import json
import sys

count = 0

def emit_card():
    card = {
        "type": "AdaptiveCard",
        "body": [
            {"type": "TextBlock", "text": f"Greeted {count} time(s)", "size": "large"},
            {"type": "Input.Text", "id": "name", "title": "Your name", "placeholder": "Ada"},
        ],
        "actions": [{"type": "Action.Submit", "title": "Say hi"}],
    }
    print(json.dumps(card), flush=True)

emit_card()
for line in sys.stdin:
    count += 1
    emit_card()
```

Running `adaptive-process:python weather_card.py` launches this, shows the
first card, and prints an updated one every time the user clicks "Say hi".
When Rouen closes the card (or the user picks a different command via a
new URI), it closes the process's stdin and then forcibly terminates it -
a script that only reads with `for line in sys.stdin` (as above) will also
exit cleanly on its own once stdin closes, but does not need to for Rouen
to clean it up.

---

## Card behavior

- **Status line**: "Running" while the process is alive, or
  "Process exited (code N)" once it has (crashed, or exited on its own -
  e.g. after reading EOF). A **Restart** button relaunches the same
  command line either way.
- **Errors**: if a stdout line fails to parse as an Adaptive Card, the
  card shows a red error message with the parse error and otherwise
  keeps showing whatever card was last valid.
- **Persistence**: the card's URI (`adaptive-process:<command line>`) is
  what gets saved to `rouen.ini` and returned by `GET /api/cards` - the
  same command relaunches on the next session, and the schema works with
  `POST /api/cards` like any other.
- **Changing the command**: sending a new locator to an existing card
  (e.g. by re-focusing its URI with a different command line) stops the
  old process and starts the new one.

---

## Implementation notes

- `src/helpers/piped_process.hpp`/`.cpp`: the general-purpose
  spawn-with-piped-stdin/stdout/stderr primitive this card is built on.
  Nothing else in the tree needed a *writable* stdin to a long-running
  child (`src/hosts/process_host.hpp` discards a child's stdout/stdin
  entirely; every other subprocess call site is a one-directional
  `popen()`), so this is new rather than reused. Reader/waiter callbacks
  run on background threads; the destructor closes stdin, force-terminates
  the process, and joins every thread before returning, so a card can
  never be handed a callback after it starts destructing itself.
- `src/cards/information/adaptive_process_card.hpp`: the card itself.
  Reuses the same `helpers::adaptive_cards::parser` and `::renderer` as
  the built-in `adaptive-card` schema and the DLL plugin styles' Adaptive
  Card adapter - only the source of the JSON differs.
- Registered in `factory.hpp` like any built-in schema; not part of the
  DLL plugin ABI (`plugin-sdk/`) at all, since there is nothing here for
  a plugin to link against.

## Security note

`adaptive-process` runs whatever command line it is given, with the same
privileges as Rouen itself. This is consistent with Rouen's existing
trust model - the Terminal card, the CMake card, and the Process
Orchestration panel already run arbitrary local commands - but it is
worth remembering before wiring a persisted layout or the REST API up to
something that accepts untrusted input.
