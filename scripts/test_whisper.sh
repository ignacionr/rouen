#!/usr/bin/env bash

# Exit on error
set -e

# Default options
DURATION=5
STREAM_MODE=false
SERVER_MODE=false
PORT=8080
MODEL_SIZE="tiny.en"
MODEL_DIR="models"

STREAM_PID=""
TEMP_WAV=""

# Robust cleanup signal trap (Ctrl+C / EXIT)
cleanup() {
    if [ -n "$STREAM_PID" ] && kill -0 "$STREAM_PID" 2>/dev/null; then
        kill -9 "$STREAM_PID" 2>/dev/null || true
    fi
    if [ -n "$REC_PID" ] && kill -0 "$REC_PID" 2>/dev/null; then
        kill -9 "$REC_PID" 2>/dev/null || true
    fi
    if [ -n "$TEMP_WAV" ] && [ -f "$TEMP_WAV" ]; then
        rm -f "$TEMP_WAV"
    fi
}
trap cleanup EXIT INT TERM

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -d, --duration SECONDS   Duration to record in seconds (default: 5)"
    echo "  -m, --model SIZE        Model size (tiny.en, base.en, small.en, etc.) (default: tiny.en)"
    echo "  -s, --stream            Use whisper-stream for real-time live output"
    echo "      --server            Force starting background whisper-server if not running"
    echo "  -p, --port PORT         whisper-server port (default: 8080)"
    echo "  -h, --help              Show this help message"
    echo ""
    exit 0
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -m|--model)
            MODEL_SIZE="$2"
            shift 2
            ;;
        -s|--stream)
            STREAM_MODE=true
            shift 1
            ;;
        --server)
            SERVER_MODE=true
            shift 1
            ;;
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            if [[ "$1" =~ ^[0-9]+$ ]]; then
                DURATION="$1"
                shift 1
            else
                echo "Unknown option: $1"
                usage
            fi
            ;;
    esac
done

MODEL_NAME="ggml-${MODEL_SIZE}.bin"
MODEL_PATH="${MODEL_DIR}/${MODEL_NAME}"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}"

# Fast binary resolution without slow disk recursion
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

WHISPER_CLI=$(find_whisper_bin "whisper-cli")
WHISPER_STREAM=$(find_whisper_bin "whisper-stream")

# Fast check if whisper-server is active (max 0.5s timeout)
IS_SERVER_RUNNING=false
if curl -s --max-time 0.5 "http://127.0.0.1:${PORT}/" >/dev/null 2>&1; then
    IS_SERVER_RUNNING=true
fi

if [ "$SERVER_MODE" = true ] && [ "$IS_SERVER_RUNNING" = false ]; then
    echo "⚡ Starting background whisper-server..."
    ./scripts/whisper_server.sh start -p "$PORT" -m "$MODEL_SIZE"
    IS_SERVER_RUNNING=true
fi

echo "=================================================="
echo "🎙️  Whisper.cpp Audio Transcription Test"
echo "--------------------------------------------------"
echo " Recording Duration : ${DURATION} seconds"
echo " Model              : ${MODEL_PATH}"
if [ "$STREAM_MODE" = true ]; then
    echo " Processing Engine  : Live Stream (whisper-stream)"
elif [ "$IS_SERVER_RUNNING" = true ]; then
    echo " Processing Engine  : Instant Server API (http://127.0.0.1:${PORT})"
else
    echo " Processing Engine  : CLI Binary (whisper-cli)"
fi
echo "=================================================="
echo ""

if [ "$STREAM_MODE" = true ]; then
    if [ -z "$WHISPER_STREAM" ]; then
        echo "❌ Error: whisper-stream binary not found."
        exit 1
    fi
    mkdir -p "$MODEL_DIR"
    if [ ! -f "$MODEL_PATH" ]; then
        echo "📥 Downloading model '${MODEL_NAME}'..."
        curl -L -o "$MODEL_PATH" "$MODEL_URL"
    fi
    echo "🎤 Starting live whisper-stream for ${DURATION} seconds (Press Ctrl+C to stop at any time)..."
    echo "--> Speak into your microphone... <--"
    echo ""
    "$WHISPER_STREAM" -m "$MODEL_PATH" -t 4 --step 2000 --length 5000 &
    STREAM_PID=$!
    sleep "$DURATION"
    kill -9 "$STREAM_PID" 2>/dev/null || true
    STREAM_PID=""
    echo ""
    echo "✅ Streaming stopped."
else
    TEMP_WAV=$(mktemp /tmp/whisper_test_XXXXXX.wav)

    if command -v rec >/dev/null 2>&1; then
        rec -q -c 1 -r 16000 -b 16 "$TEMP_WAV" rate 16000 trim 0 "$DURATION" 2>/dev/null &
        REC_PID=$!
    elif command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -y -loglevel quiet -f avfoundation -i ":default" -ar 16000 -ac 1 -t "$DURATION" "$TEMP_WAV" &
        REC_PID=$!
    else
        echo "❌ Error: Neither 'rec' (SoX) nor 'ffmpeg' found for audio recording."
        exit 1
    fi

    # Live countdown display during recording
    for (( i=DURATION; i>0; i-- )); do
        printf "\r🎤 Recording live microphone audio... %2d seconds remaining [Speak now!] " "$i"
        sleep 1
    done
    wait "$REC_PID" 2>/dev/null || true
    REC_PID=""
    printf "\r🎤 Recording complete! Transcribing audio...                             \n"
    echo ""

    echo "------------------- TRANSCRIPT -------------------"
    if [ "$IS_SERVER_RUNNING" = true ]; then
        # Send audio directly to whisper-server endpoint (Instant ~100ms response!)
        curl -s "http://127.0.0.1:${PORT}/inference" \
            -F "file=@${TEMP_WAV}" \
            -F "response_format=text" | sed 's/^[[:space:]]*//'
    else
        if [ -z "$WHISPER_CLI" ]; then
            echo "❌ Error: whisper-cli executable not found."
            exit 1
        fi
        mkdir -p "$MODEL_DIR"
        if [ ! -f "$MODEL_PATH" ]; then
            echo "📥 Downloading model '${MODEL_NAME}'..."
            curl -L -o "$MODEL_PATH" "$MODEL_URL"
        fi
        "$WHISPER_CLI" -m "$MODEL_PATH" -f "$TEMP_WAV" -nt -np
    fi
    echo "--------------------------------------------------"
fi

echo ""
