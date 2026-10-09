#!/usr/bin/env zsh
# Rouen Adaptive Process: Inbox Status Dashboard
# Emits compact single-line Adaptive Card JSON to stdout and handles Action.Submit on stdin.

INBOX_DIR="${1:-$HOME/rouen/inbox}"
if [[ ! -d "$INBOX_DIR" ]]; then
    if [[ -d "$HOME/src/rouen/inbox" ]]; then
        INBOX_DIR="$HOME/src/rouen/inbox"
    elif [[ -d "./inbox" ]]; then
        INBOX_DIR="./inbox"
    fi
fi

generate_card() {
    local done_count=0
    local pending_count=0
    local in_progress_count=0

    local done_items=()
    local pending_items=()
    local in_progress_items=()

    if [[ -d "$INBOX_DIR" ]]; then
        for f in "$INBOX_DIR"/*.md; do
            [[ -e "$f" ]] || continue
            local filename="$(basename "$f")"
            [[ "$filename" == "README.md" ]] && continue

            local title=""
            if [[ -f "$f" ]]; then
                title=$(grep -m 1 '^# ' "$f" 2>/dev/null | sed 's/^# //')
            fi
            [[ -z "$title" ]] && title="$filename"
            # Escape quotes and backslashes
            title="${title//\\/\\\\}"
            title="${title//\"/\\\"}"

            if [[ "$filename" == done_* ]]; then
                ((done_count++))
                if (( ${#done_items[@]} < 8 )); then
                    done_items+=("{\"type\":\"TextBlock\",\"text\":\"- $title\",\"wrap\":true,\"isSubtle\":true,\"size\":\"small\"}")
                fi
            elif grep -qi "status:.*in.progress" "$f" 2>/dev/null || grep -qi "\*\*Status\*\*:.*in.progress" "$f" 2>/dev/null; then
                ((in_progress_count++))
                if (( ${#in_progress_items[@]} < 8 )); then
                    in_progress_items+=("{\"type\":\"TextBlock\",\"text\":\"- $title\",\"wrap\":true,\"color\":\"accent\",\"weight\":\"bolder\",\"size\":\"small\"}")
                fi
            else
                ((pending_count++))
                if (( ${#pending_items[@]} < 8 )); then
                    pending_items+=("{\"type\":\"TextBlock\",\"text\":\"- $title\",\"wrap\":true,\"size\":\"small\"}")
                fi
            fi
        done
    fi

    local timestamp="$(date '+%Y-%m-%d %H:%M:%S')"

    # Build JSON
    local card="{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"refreshIntervalMs\":5000,\"body\":["
    card+="{\"type\":\"TextBlock\",\"text\":\"Rouen Inbox Status\",\"size\":\"medium\",\"weight\":\"bolder\",\"color\":\"accent\"},"
    card+="{\"type\":\"TextBlock\",\"text\":\"Folder: $INBOX_DIR | Updated: $timestamp\",\"size\":\"small\",\"isSubtle\":true,\"spacing\":\"none\"},"
    card+="{\"type\":\"FactSet\",\"facts\":["
    card+="{\"title\":\"In Progress:\",\"value\":\"$in_progress_count\"},"
    card+="{\"title\":\"Pending:\",\"value\":\"$pending_count\"},"
    card+="{\"title\":\"Completed:\",\"value\":\"$done_count\"}"
    card+="]},"

    # In Progress section
    card+="{\"type\":\"TextBlock\",\"text\":\"In Progress ($in_progress_count)\",\"weight\":\"bolder\",\"color\":\"warning\"}"
    if (( in_progress_count > 0 )); then
        local joined_in_progress=$(IFS=,; echo "${in_progress_items[*]}")
        card+=",$joined_in_progress"
    else
        card+=",{\"type\":\"TextBlock\",\"text\":\"None currently in progress.\",\"isSubtle\":true,\"size\":\"small\"}"
    fi

    # Pending section
    card+=",{\"type\":\"TextBlock\",\"text\":\"Pending ($pending_count)\",\"weight\":\"bolder\",\"color\":\"accent\"}"
    if (( pending_count > 0 )); then
        local joined_pending=$(IFS=,; echo "${pending_items[*]}")
        card+=",$joined_pending"
    else
        card+=",{\"type\":\"TextBlock\",\"text\":\"No pending items.\",\"isSubtle\":true,\"size\":\"small\"}"
    fi

    # Done section
    card+=",{\"type\":\"TextBlock\",\"text\":\"Completed ($done_count)\",\"weight\":\"bolder\",\"color\":\"good\"}"
    if (( done_count > 0 )); then
        local joined_done=$(IFS=,; echo "${done_items[*]}")
        card+=",$joined_done"
    fi

    card+="],\"actions\":[{\"type\":\"Action.Submit\",\"title\":\"Refresh Now\",\"data\":{\"action\":\"refresh\"}}]}"

    # Output strictly on one single line
    print -r -- "$card"
}

# Initial card output immediately
generate_card

# Loop: refresh every 5 seconds or upon receiving Action.Submit from stdin
while true; do
    if read -t 5 -r line; then
        generate_card
    else
        generate_card
    fi
done
