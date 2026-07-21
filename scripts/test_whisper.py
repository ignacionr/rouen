#!/usr/bin/env python3
"""
Whisper.cpp Real-Time Audio Transcription Test Script

Records audio from the default microphone for a specified duration (in seconds)
and transcribes it locally.

If `whisper-server` is running (or `--server` flag is provided), requests are
sent to the HTTP endpoint for INSTANT (~100ms) transcription without reloading the model.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

DEFAULT_MODEL = "tiny.en"
MODEL_DIR = "models"
SERVER_URL = "http://127.0.0.1:8080"


def find_executable(name):
    path = shutil.which(name)
    if path:
        return path
    nix_store = "/nix/store"
    if os.path.isdir(nix_store):
        matches = glob.glob(f"/nix/store/*whisper-cpp*/bin/{name}")
        if matches:
            return matches[0]
    return None


def is_server_running(url=SERVER_URL):
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=0.5) as resp:
            return resp.status == 200
    except Exception:
        return False


def ensure_model(model_size):
    os.makedirs(MODEL_DIR, exist_ok=True)
    model_file = f"ggml-{model_size}.bin"
    model_path = os.path.join(MODEL_DIR, model_file)

    if not os.path.exists(model_path):
        url = f"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/{model_file}"
        print(f"📥 Downloading model '{model_file}' from Hugging Face...")
        urllib.request.urlretrieve(url, model_path)
        print("✅ Download complete.")

    return model_path


def record_audio(output_wav, duration_sec):
    rec = shutil.which("rec")
    ffmpeg = shutil.which("ffmpeg")

    if rec:
        cmd = [
            rec,
            "-q",
            "-c",
            "1",
            "-r",
            "16000",
            "-b",
            "16",
            output_wav,
            "rate",
            "16000",
            "trim",
            "0",
            str(duration_sec),
        ]
    elif ffmpeg:
        cmd = [
            ffmpeg,
            "-y",
            "-loglevel",
            "quiet",
            "-f",
            "avfoundation",
            "-i",
            ":default",
            "-ar",
            "16000",
            "-ac",
            "1",
            "-t",
            str(duration_sec),
            output_wav,
        ]
    else:
        sys.exit("❌ Error: Neither 'rec' (SoX) nor 'ffmpeg' found for recording audio.")

    proc = subprocess.Popen(cmd, stderr=subprocess.DEVNULL)
    for i in range(duration_sec, 0, -1):
        sys.stdout.write(f"\r🎤 Recording live microphone audio... {i:2d} seconds remaining [Speak now!] ")
        sys.stdout.flush()
        time.sleep(1)

    proc.wait()
    sys.stdout.write("\r🎤 Recording complete! Transcribing audio...                             \n\n")
    sys.stdout.flush()


def transcribe_via_server(audio_wav, url=SERVER_URL):
    print("------------------- TRANSCRIPT -------------------")
    cmd = [
        "curl",
        "-s",
        f"{url}/inference",
        "-F",
        f"file=@{audio_wav}",
        "-F",
        "response_format=text",
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    print(res.stdout.strip())
    print("--------------------------------------------------")


def transcribe_via_cli(whisper_cli, model_path, audio_wav):
    print("------------------- TRANSCRIPT -------------------")
    cmd = [whisper_cli, "-m", model_path, "-f", audio_wav, "-nt", "-np"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    print(res.stdout.strip())
    print("--------------------------------------------------")


def main():
    parser = argparse.ArgumentParser(description="Local audio test for whisper.cpp")
    parser.add_argument(
        "-d",
        "--duration",
        type=int,
        default=5,
        help="Recording duration in seconds (default: 5)",
    )
    parser.add_argument(
        "-m",
        "--model",
        type=str,
        default=DEFAULT_MODEL,
        help="Model size (tiny.en, base.en, small.en, etc.)",
    )
    parser.add_argument(
        "--server",
        action="store_true",
        help="Force starting/using background whisper-server",
    )
    args = parser.parse_args()

    server_active = is_server_running()

    if args.server and not server_active:
        print("⚡ Starting background whisper-server...")
        subprocess.run(["./scripts/whisper_server.sh", "start", "-m", args.model])
        server_active = True

    print("==================================================")
    print("🎙️  Whisper.cpp Audio Transcription Test")
    print("--------------------------------------------------")
    print(f" Recording Duration : {args.duration} seconds")
    print(f" Model              : {args.model}")
    print(f" Processing Engine  : {'Instant Server API (' + SERVER_URL + ')' if server_active else 'CLI Binary (whisper-cli)'}")
    print("==================================================")
    print()

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as tmp:
        record_audio(tmp.name, args.duration)
        if server_active:
            transcribe_via_server(tmp.name)
        else:
            whisper_cli = find_executable("whisper-cli")
            if not whisper_cli:
                sys.exit("❌ Error: whisper-cli not found.")
            model_path = ensure_model(args.model)
            transcribe_via_cli(whisper_cli, model_path, tmp.name)


if __name__ == "__main__":
    main()
