#!/usr/bin/env bash
# scripts/process-inbox.sh
# Autonomously process Rouen inbox tasks using Antigravity CLI (agy) in a detached session.
# Designed for macOS execution so background processing persists across SSH disconnects.

set -o pipefail

# 1. Environment & Path Normalization
if [ -f "$HOME/.zprofile" ]; then
    source "$HOME/.zprofile" 2>/dev/null || true
fi
if [ -f "$HOME/.zshrc" ]; then
    source "$HOME/.zshrc" 2>/dev/null || true
fi

export PATH="$HOME/bin:$HOME/.local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/nix/var/nix/profiles/default/bin:$HOME/.nix-profile/bin:$PATH"
export ANTIGRAVITY_ENV="production"

# 2. Determine Repository Root & Paths
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INBOX_DIR="$REPO_ROOT/inbox"
RUN_DIR="$REPO_ROOT/.rouen"
PID_FILE="$RUN_DIR/inbox_runner.pid"
LOG_FILE="$RUN_DIR/inbox_runner.log"

# 3. Locate agy Binary
find_agy() {
    if command -v agy >/dev/null 2>&1; then
        command -v agy
    elif [ -x "$HOME/bin/agy" ]; then
        echo "$HOME/bin/agy"
    elif [ -x "$HOME/.local/bin/agy" ]; then
        echo "$HOME/.local/bin/agy"
    else
        local nix_agy
        nix_agy=$(find /nix/store -maxdepth 3 -name agy -type f 2>/dev/null | sort -V | tail -n 1)
        if [ -n "$nix_agy" ] && [ -x "$nix_agy" ]; then
            echo "$nix_agy"
        fi
    fi
}

AGY_BIN="$(find_agy)"

log() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] $*"
    echo "$msg"
}

is_running() {
    if [ -f "$PID_FILE" ]; then
        local pid
        pid=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

get_pending_items() {
    if [ ! -d "$INBOX_DIR" ]; then
        return 0
    fi
    find "$INBOX_DIR" -maxdepth 1 -type f -name "*.md" | while read -r file; do
        local bname
        bname=$(basename "$file")
        case "$bname" in
            README.md|done_*|done-*|rejected_*|rejected-*|.*)
                continue
                ;;
            *)
                echo "$file"
                ;;
        esac
    done | sort
}

count_pending_items() {
    get_pending_items | wc -l | tr -d ' '
}

count_done_items() {
    if [ ! -d "$INBOX_DIR" ]; then
        echo "0"
        return 0
    fi
    find "$INBOX_DIR" -maxdepth 1 -type f \( -name "done_*.md" -o -name "done-*.md" \) | wc -l | tr -d ' '
}

start_daemon() {
    if [ -z "$AGY_BIN" ]; then
        echo "Error: agy binary not found in PATH, ~/bin, ~/.local/bin, or /nix/store." >&2
        exit 1
    fi

    if is_running; then
        local existing_pid
        existing_pid=$(cat "$PID_FILE" 2>/dev/null)
        echo "Inbox processor is already running (PID: $existing_pid)."
        show_status
        return 0
    fi

    mkdir -p "$RUN_DIR"
    touch "$LOG_FILE"

    echo "Starting detached inbox processor daemon..."
    nohup "$0" run >> "$LOG_FILE" 2>&1 < /dev/null &
    local new_pid=$!
    echo "$new_pid" > "$PID_FILE"
    disown "$new_pid" 2>/dev/null || true

    sleep 1
    if kill -0 "$new_pid" 2>/dev/null; then
        echo "Started successfully in background."
        echo "  PID:      $new_pid"
        echo "  Log:      $LOG_FILE"
        echo "  Pending:  $(count_pending_items) item(s)"
        echo ""
        echo "Use '$0 status' to monitor progress or '$0 stop' to terminate."
    else
        echo "Error: Failed to start processor in background. Inspect $LOG_FILE:" >&2
        tail -n 20 "$LOG_FILE"
        exit 1
    fi
}

stop_daemon() {
    if [ -f "$PID_FILE" ]; then
        local pid
        pid=$(cat "$PID_FILE" 2>/dev/null)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            echo "Stopping inbox processor (PID: $pid)..."
            kill "$pid" 2>/dev/null || true
            for _ in {1..10}; do
                if ! kill -0 "$pid" 2>/dev/null; then
                    break
                fi
                sleep 0.5
            done
            if kill -0 "$pid" 2>/dev/null; then
                echo "Process did not exit gracefully, sending SIGKILL..."
                kill -9 "$pid" 2>/dev/null || true
            fi
            echo "Stopped."
        else
            echo "Inbox processor PID file exists ($pid), but process is not running."
        fi
        rm -f "$PID_FILE"
    else
        echo "Inbox processor is not running (no PID file found)."
    fi
}

show_status() {
    local pending_count done_count
    pending_count=$(count_pending_items)
    done_count=$(count_done_items)

    echo "=================================================="
    echo "          Rouen Inbox Processor Status            "
    echo "=================================================="
    echo "Working Tree: $REPO_ROOT"
    echo "AGY Binary:   ${AGY_BIN:-[NOT FOUND]}"
    echo "Inbox Queue:  $done_count completed | $pending_count pending"

    if is_running; then
        local pid
        pid=$(cat "$PID_FILE" 2>/dev/null)
        echo "Status:       RUNNING (PID: $pid)"
    else
        echo "Status:       STOPPED"
    fi

    echo "Log File:     $LOG_FILE"
    echo "--------------------------------------------------"
    if [ -f "$LOG_FILE" ]; then
        echo "Recent Log Entries (last 15 lines):"
        tail -n 15 "$LOG_FILE"
    else
        echo "No log file found yet."
    fi
    echo "=================================================="
}

run_worker() {
    if [ -z "$AGY_BIN" ]; then
        log "Error: agy binary not found."
        exit 1
    fi

    mkdir -p "$RUN_DIR"
    echo "$$" > "$PID_FILE"

    cleanup() {
        log "Inbox runner exiting (PID $$)..."
        rm -f "$PID_FILE"
    }
    trap cleanup EXIT INT TERM

    cd "$REPO_ROOT" || exit 1

    log "=== Starting Rouen Inbox Processing Daemon (PID $$) ==="
    log "Working Directory: $REPO_ROOT"
    log "AGY Binary:        $AGY_BIN"
    log "Pending items:     $(count_pending_items)"

    declare -A item_failures

    while true; do
        local pending_list
        pending_list=$(get_pending_items)

        if [ -z "$pending_list" ]; then
            log "No pending inbox items remaining. Inbox processing fully complete!"
            if command -v say >/dev/null 2>&1; then
                say "All inbox items completed" 2>/dev/null || true
            fi
            break
        fi

        local current_item
        current_item=$(echo "$pending_list" | head -n 1)
        local item_name
        item_name=$(basename "$current_item")

        local failures="${item_failures[$item_name]:-0}"
        if [ "$failures" -ge 2 ]; then
            log "WARNING: Item $item_name failed $failures consecutive attempts. Marking skipped to avoid deadlock."
            local skipped_path="$INBOX_DIR/skipped_$item_name"
            mv "$current_item" "$skipped_path" 2>/dev/null || true
            continue
        fi

        log ">>> Processing inbox item: $item_name (Attempt $((failures + 1))) <<<"

        local prompt
        prompt="You are an autonomous senior software engineer working in the Rouen repository at: $REPO_ROOT.

Your task is to process this inbox specification file end-to-end:
$current_item

Follow the 9-step issue lifecycle strictly:
1. Understand: Read the report and trace affected architecture and code paths.
2. Diagnose: Pinpoint root causes across affected systems.
3. Setup Unit Test: Add or expand unit tests in tests/ covering the bug/feature.
4. Run and Fail: Run the test to confirm it fails as expected (red).
5. Fix: Implement the minimal, clean fix in src/.
   CRITICAL BUILD CONSTRAINT: Strictly limit parallel compile jobs to at most 2 (-j2 or --max-jobs 2). Never omit -j or set it higher than 2.
   Quick build command: nix develop --command cmake --build build --target rouen -j2
6. Run and Pass: Run unit tests to verify they pass (green) and target builds cleanly.
7. Commit: Create a conventional commit (fix(...) or feat(...)).
8. Push: Push commit to remote origin.
9. Rename Report: Rename the inbox file $current_item to mark it done (prefix with done_ or insert done- after date prefix).

Do not ask for user permission or pause with speculative questions; carry the task all the way through to completion."

        # Run agy with auto-approval and print mode
        log "Launching agy execution turn..."
        "$AGY_BIN" --dangerously-skip-permissions -p "$prompt" 2>&1
        local exit_code=$?

        # Verify outcome
        if [ ! -f "$current_item" ]; then
            log "SUCCESS: $item_name was processed and marked done (exit code $exit_code)."
            unset "item_failures[$item_name]"
        else
            failures=$((failures + 1))
            item_failures[$item_name]=$failures
            log "NOTICE: $item_name still exists after run (attempt $failures, exit code $exit_code)."
        fi

        sleep 3
    done

    log "=== Inbox processing daemon completed successfully ==="
}

# 4. Command Router
ACTION="${1:-start}"

case "$ACTION" in
    start|--daemon|--background)
        start_daemon
        ;;
    run|--foreground)
        run_worker
        ;;
    stop)
        stop_daemon
        ;;
    status)
        show_status
        ;;
    log|--log)
        touch "$LOG_FILE"
        tail -f "$LOG_FILE"
        ;;
    help|--help|-h)
        echo "Usage: $0 {start|status|stop|run|log}"
        echo "  start   - Launch inbox processor in background (detached, survives SSH disconnects)"
        echo "  status  - Show processor status, item counts, and recent logs"
        echo "  stop    - Gracefully terminate background processor"
        echo "  run     - Run processor directly in foreground"
        echo "  log     - Follow runner log file ($LOG_FILE)"
        ;;
    *)
        echo "Unknown option: $ACTION" >&2
        echo "Usage: $0 {start|status|stop|run|log}" >&2
        exit 1
        ;;
esac
