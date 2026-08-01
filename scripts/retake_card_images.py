#!/usr/bin/env python3
import json
import time
import urllib.request
import urllib.error
import os
import sys

API_BASE = "http://127.0.0.1:8081/api"
IMAGES_DIR = "/Users/ignaciorodriguez/src/rouen/docs/cards/images"

def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)

def api_post(endpoint, payload=None):
    url = f"{API_BASE}/{endpoint}"
    data = json.dumps(payload).encode('utf-8') if payload is not None else b''
    req = urllib.request.Request(
        url,
        data=data,
        headers={'Content-Type': 'application/json'}
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            body = resp.read().decode('utf-8')
            return json.loads(body) if body else {}
    except Exception as e:
        log(f"API POST Error ({endpoint}): {e}")
        return {"error": str(e)}

def api_get(endpoint):
    url = f"{API_BASE}/{endpoint}"
    req = urllib.request.Request(url)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            body = resp.read().decode('utf-8')
            return json.loads(body) if body else {}
    except Exception as e:
        log(f"API GET Error ({endpoint}): {e}")
        return {"error": str(e)}

def get_image_dimensions(filepath):
    import struct
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'rb') as f:
        data = f.read(30)
        if data[:8] == b'\x89PNG\r\n\x1a\n':
            w, h = struct.unpack('>II', data[16:24])
            return w, h
    return None

def main():
    log("=== Rouen API Card Snapshot Generator ===")
    
    # 1. Check API Health
    health = api_get("health")
    log(f"API Health: {health}")
    if health.get("status") != "ok":
        log("Error: Rouen API server is not running!")
        sys.exit(1)

    # 2. Resize Window for adequate vertical and horizontal space
    log("Resizing Rouen window to 1600x900...")
    resize_resp = api_post("window", {"width": 1600, "height": 900})
    log(f"Window resize response: {resize_resp}")
    time.sleep(1)

    feed_id = 1
    rss_feed_uri = f"rss-feed:{feed_id}"
    rss_item_uri = f"rss-item:{feed_id}|||https://news.ycombinator.com/|||Hacker News"

    # Define the 6 target cards with their URIs, snapshot targets, output files, and data load wait times
    cards_config = [
        {
            "name": "Calendar Card",
            "uri": "calendar",
            "target": "calendar",
            "filename": os.path.join(IMAGES_DIR, "card_calendar.png"),
            "wait_time": 2
        },
        {
            "name": "Weather Forecast Card",
            "uri": "weather:Montevideo",
            "target": "weather",
            "filename": os.path.join(IMAGES_DIR, "card_weather.png"),
            "wait_time": 4
        },
        {
            "name": "Wikipedia Card",
            "uri": "wikipedia:title:Quantum_computing",
            "target": "wikipedia",
            "filename": os.path.join(IMAGES_DIR, "card_wikipedia.png"),
            "wait_time": 5
        },
        {
            "name": "Contacts Directory Card",
            "uri": "contacts",
            "target": "contacts",
            "filename": os.path.join(IMAGES_DIR, "card_contacts.png"),
            "wait_time": 2
        },
        {
            "name": "RSS Feed Channel Card",
            "uri": rss_feed_uri,
            "target": "rss-feed",
            "filename": os.path.join(IMAGES_DIR, "card_rss_feed.png"),
            "wait_time": 5
        },
        {
            "name": "RSS Feed Item Reader Card",
            "uri": rss_item_uri,
            "target": "rss-item",
            "filename": os.path.join(IMAGES_DIR, "card_rss_item.png"),
            "wait_time": 4
        }
    ]

    for config in cards_config:
        log(f"\n----------------------------------------")
        log(f"Processing: {config['name']}")
        log(f"URI: {config['uri']}")
        log(f"Target file: {config['filename']}")

        # Step A: Create card
        create_resp = api_post("cards", {"uri": config["uri"]})
        log(f"Create response: {create_resp}")

        # Step B: Focus card
        focus_resp = api_post("cards/focus", {"uri": config["uri"]})
        log(f"Focus response: {focus_resp}")

        # Step C: Wait enough time for data to load
        log(f"Waiting {config['wait_time']}s for data to load...")
        time.sleep(config['wait_time'])

        # Step D: Take snapshot
        log(f"Requesting snapshot for target '{config['target']}'...")
        snap_resp = api_post("screenshot", {
            "target": config["target"],
            "filename": config["filename"]
        })
        log(f"Snapshot response: {snap_resp}")

        # Step E: Wait a bit for filesystem flush
        log("Waiting 2s for image file writing...")
        time.sleep(2)

        # Step F: Double check dimensions
        dims = get_image_dimensions(config["filename"])
        if dims:
            log(f"SUCCESS: Snapshot saved. Dimensions: {dims[0]}x{dims[1]} pixels.")
        else:
            log(f"ERROR: Image file not found or invalid: {config['filename']}")

    log("\n========================================")
    log("All card images processed. Verifying final dimensions:")
    for config in cards_config:
        dims = get_image_dimensions(config["filename"])
        log(f"- {os.path.basename(config['filename'])}: {dims[0]}x{dims[1]} px" if dims else f"- {os.path.basename(config['filename'])}: MISSING")

if __name__ == "__main__":
    main()
