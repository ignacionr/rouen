#!/usr/bin/env bash

set -e

PORT=8080
MODEL_SIZE="tiny.en"
MODEL_DIR="models"
PID_FILE=".whisper_server.pid"
LOG_FILE="whisper_server.log"

MODEL_NAME="ggml-${MODEL_SIZE}.bin"
MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}"

# Fast path resolution
find_whisper_bin() {
    local name="$1"
    local bin
    bin=$(which "$name" 2>/dev/null || true)
    if [ -n "$bin" ]; then
        echo "$bin"
        return
    fi
    bin=$(ls -d /nix/store/*whisper-cpp*/bin/"$name" 2>/dev/null | head -n 1 || true)
    if [ -n "$bin" ]; then
        echo "$bin"
        return
    fi
}

WHISPER_SERVER=$(find_whisper_bin "whisper-server")

usage() {
    echo "Usage: $0 {start|stop|status|restart} [options]"
    echo ""
    echo "Options:"
    echo "  -p, --port PORT     Server port (default: 8080)"
    echo "  -m, --model SIZE    Model size (default: tiny.en)"
    echo ""
    exit 1
}

ACTION="$1"
shift || true

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -m|--model)
            MODEL_SIZE="$2"
            MODEL_NAME="ggml-${MODEL_SIZE}.bin"
            MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"
            MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

if [ -z "$WHISPER_SERVER" ]; then
    echo "❌ Error: whisper-server binary not found."
    exit 1
fi

start_server() {
    if [ -f "$PID_FILE" ] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
        echo "⚡ whisper-server is already running (PID $(cat "$PID_FILE"))."
        exit 0
    fi

    mkdir -p "$MODEL_DIR"
    if [ ! -f "$MODEL_PATH" ]; then
        echo "📥 Downloading model '${MODEL_NAME}'..."
        curl -L -o "$MODEL_PATH" "$MODEL_URL"
    fi

    echo "🚀 Starting whisper-server on port ${PORT} using ${MODEL_PATH}..."
    nohup "$WHISPER_SERVER" -m "$MODEL_PATH" --port "$PORT" --convert > "$LOG_FILE" 2>&1 &
    PID=$!
    echo "$PID" > "$PID_FILE"

    # Wait briefly to confirm startup
    sleep 2
    if kill -0 "$PID" 2>/dev/null; then
        echo "✅ whisper-server started successfully (PID ${PID}). Log: ${LOG_FILE}"
    else
        echo "❌ Failed to start whisper-server. Check ${LOG_FILE} for details."
        rm -f "$PID_FILE"
        exit 1
    fi
}

stop_server() {
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "🛑 Stopping whisper-server (PID ${PID})..."
            kill "$PID" 2>/dev/null || true
            sleep 1
        fi
        rm -f "$PID_FILE"
        echo "✅ Server stopped."
    else
        PIDS=$(pgrep -f "whisper-server" || true)
        if [ -n "$PIDS" ]; then
            echo "🛑 Stopping running whisper-server processes: ${PIDS}..."
            kill $PIDS 2>/dev/null || true
            echo "✅ Server stopped."
        else
            echo "ℹ️  whisper-server is not running."
        fi
    fi
}

status_server() {
    if [ -f "$PID_FILE" ] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
        echo "🟢 whisper-server is RUNNING (PID $(cat "$PID_FILE")) on http://127.0.0.1:${PORT}"
    else
        PIDS=$(pgrep -f "whisper-server" || true)
        if [ -n "$PIDS" ]; then
            echo "🟢 whisper-server is RUNNING (PID ${PIDS}) on http://127.0.0.1:${PORT}"
        else
            echo "🔴 whisper-server is STOPPED."
        fi
    fi
}

case "$ACTION" in
    start)
        start_server
        ;;
    stop)
        stop_server
        ;;
    restart)
        stop_server
        start_server
        ;;
    status)
        status_server
        ;;
    *)
        usage
        ;;
esac
