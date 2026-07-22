#!/usr/bin/env bash
# Script to play Rouen's constant video feed in mpv with zero buffering and low latency

PORT="${1:-8889}"
URL="udp://127.0.0.1:${PORT}"

echo "[INFO] Connecting mpv to Rouen video feed at ${URL}..."

exec mpv \
    --profile=high-quality \
    --no-cache \
    --framedrop=vo \
    --demuxer-lavf-o=fflags=nobuffer \
    "${URL}"
