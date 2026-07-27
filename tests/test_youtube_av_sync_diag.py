#!/usr/bin/env python3
"""
YouTube A/V Synchronization Diagnostic Test
============================================

This test measures the actual audio-video synchronization mismatch during
YouTube playback in Rouen. YouTube content comes as separate audio and video
streams (resolved via yt-dlp), making precise A/V sync particularly
challenging.

The test polls the internal A/V sync diagnostic telemetry exposed at
/api/cast/status and asserts that:

  1. av_sync_delta_ms stays within ±80ms (broadcast-quality threshold)
  2. No excessive frame drops (< 10% of presented frames)
  3. Playback starts within 15 seconds
  4. Both audio and video streams are detected (first_audio_pts and
     first_video_pts are populated)
  5. The PTS offset between audio and video first-packets is reasonable
     (< 2.0s apart)

EXPECTED: This test FAILS with the current implementation due to known
A/V synchronization issues with separate YouTube streams.

Usage:
  python3 test_youtube_av_sync_diag.py [--url YOUTUBE_URL]
"""

import time
import json
import urllib.request
import subprocess
import sys
import argparse
import statistics

# ─── Configuration ───────────────────────────────────────────────────────────

ROUEN_API_BASE = "http://127.0.0.1:8081"
DEFAULT_YOUTUBE_URL = "https://www.youtube.com/watch?v=jNQXAC9IVRw"  # Me at the zoo (short 19s video)

# Thresholds
MAX_AV_SYNC_DELTA_MS = 80.0      # Broadcast-quality sync tolerance
MAX_FRAME_DROP_RATIO = 0.10      # Max 10% of frames may be dropped
MAX_STARTUP_WAIT_S = 15.0        # Max seconds to wait for playback to begin
MAX_STREAM_PTS_OFFSET_S = 2.0    # Max PTS offset between first audio and first video
MONITORING_DURATION_S = 25.0     # How long to monitor sync after playback starts
POLL_INTERVAL_S = 0.25           # 4 Hz polling rate

# ─── Helpers ─────────────────────────────────────────────────────────────────

def is_rouen_running():
    try:
        out = subprocess.check_output(["pgrep", "rouen"]).decode().strip()
        return len(out) > 0
    except Exception:
        return False

def api_get(path):
    """GET request to Rouen API, returns parsed JSON dict or None."""
    try:
        with urllib.request.urlopen(f"{ROUEN_API_BASE}{path}", timeout=5) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except Exception:
        return None

def api_post(path, payload):
    """POST JSON to Rouen API, returns response text or None."""
    try:
        req = urllib.request.Request(
            f"{ROUEN_API_BASE}{path}",
            data=json.dumps(payload).encode('utf-8'),
            headers={'Content-Type': 'application/json'}
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.read().decode('utf-8')
    except Exception as e:
        return None

# ─── Test ────────────────────────────────────────────────────────────────────

def run_av_sync_diagnostic_test(youtube_url):
    print("=" * 78)
    print("  YOUTUBE A/V SYNCHRONIZATION DIAGNOSTIC TEST")
    print("=" * 78)
    print(f"  URL:    {youtube_url}")
    print(f"  Thresholds:")
    print(f"    Max A/V sync delta:   ±{MAX_AV_SYNC_DELTA_MS:.0f} ms")
    print(f"    Max frame drop ratio: {MAX_FRAME_DROP_RATIO * 100:.0f}%")
    print(f"    Max startup wait:     {MAX_STARTUP_WAIT_S:.0f} s")
    print(f"    Monitoring duration:  {MONITORING_DURATION_S:.0f} s")
    print("=" * 78)

    # ── Pre-flight check ──────────────────────────────────────────────────
    if not is_rouen_running():
        print("\n[FAIL] Rouen process is NOT running!")
        return False

    # ── Phase 1: Trigger YouTube playback ─────────────────────────────────
    print("\n[Phase 1] Triggering YouTube media playback via API...")
    result = api_post("/api/cards", {"uri": f"media:{youtube_url}"})
    if result is None:
        print("[FAIL] Failed to trigger YouTube card via API!")
        return False
    print(f"  API response: {result}")

    # ── Phase 2: Wait for playback to start ───────────────────────────────
    print(f"\n[Phase 2] Waiting up to {MAX_STARTUP_WAIT_S:.0f}s for playback to start...")
    startup_start = time.time()
    playback_started = False

    while time.time() - startup_start < MAX_STARTUP_WAIT_S:
        if not is_rouen_running():
            print("[FAIL] Rouen process crashed during startup!")
            return False

        data = api_get("/api/cast/status")
        if data and data.get("is_media_playing", False) and data.get("has_video", False):
            first_v = data.get("first_video_pts", -1.0)
            first_a = data.get("first_audio_pts", -1.0)
            if first_v >= 0.0 and first_a >= 0.0:
                elapsed = time.time() - startup_start
                print(f"  Playback started after {elapsed:.1f}s")
                print(f"    first_video_pts = {first_v:.6f}s")
                print(f"    first_audio_pts = {first_a:.6f}s")
                print(f"    PTS offset      = {abs(first_v - first_a):.3f}s")
                playback_started = True
                break

        time.sleep(0.5)

    if not playback_started:
        print(f"[FAIL] YouTube playback did not start with both streams within {MAX_STARTUP_WAIT_S:.0f}s!")
        # Dump last status for debugging
        data = api_get("/api/cast/status")
        if data:
            print(f"  Last status: is_playing={data.get('is_media_playing')}, has_video={data.get('has_video')}")
            print(f"               first_video_pts={data.get('first_video_pts')}, first_audio_pts={data.get('first_audio_pts')}")
        return False

    # ── Phase 3: Collect A/V sync diagnostic samples ──────────────────────
    print(f"\n[Phase 3] Collecting A/V sync samples for {MONITORING_DURATION_S:.0f}s...")
    print(f"{'#':>4} {'Elapsed':>8} {'Pos':>8} {'A/V Δ ms':>10} {'VidQ':>5} {'Presented':>10} {'Dropped':>8} {'Held':>6} {'AudioQ':>7}")
    print("-" * 78)

    samples = []
    monitor_start = time.time()
    sample_num = 0

    while time.time() - monitor_start < MONITORING_DURATION_S:
        if not is_rouen_running():
            print("\n[FAIL] Rouen process crashed during sync monitoring!")
            return False

        data = api_get("/api/cast/status")
        if data and data.get("is_media_playing", False):
            sample_num += 1
            elapsed = time.time() - monitor_start
            pos = data.get("position", 0.0)
            av_delta = data.get("av_sync_delta_ms", 0.0)
            vid_q = data.get("video_queue_size", 0)
            presented = data.get("frames_presented", 0)
            dropped = data.get("frames_dropped", 0)
            held = data.get("frames_held", 0)
            audio_q = data.get("audio_queue_seconds", 0.0)

            samples.append({
                "elapsed": elapsed,
                "position": pos,
                "av_sync_delta_ms": av_delta,
                "video_queue_size": vid_q,
                "frames_presented": presented,
                "frames_dropped": dropped,
                "frames_held": held,
                "audio_queue_seconds": audio_q,
                "first_video_pts": data.get("first_video_pts", -1.0),
                "first_audio_pts": data.get("first_audio_pts", -1.0),
                "last_presented_pts": data.get("last_presented_pts", -1.0),
            })

            # Print every 4th sample for readability
            if sample_num % 4 == 1 or abs(av_delta) > MAX_AV_SYNC_DELTA_MS:
                flag = " ⚠" if abs(av_delta) > MAX_AV_SYNC_DELTA_MS else ""
                print(f"{sample_num:4d} {elapsed:7.1f}s {pos:7.2f}s {av_delta:+9.1f}ms {vid_q:5d} {presented:10d} {dropped:8d} {held:6d} {audio_q:6.3f}s{flag}")

        time.sleep(POLL_INTERVAL_S)

    if not samples:
        print("[FAIL] No sync samples were collected!")
        return False

    # ── Phase 4: Analysis & Assertions ────────────────────────────────────
    print("\n" + "=" * 78)
    print("  A/V SYNC ANALYSIS REPORT")
    print("=" * 78)

    # Only analyze samples where frames are being actively presented
    active_samples = [s for s in samples if s["frames_presented"] > 0]
    if not active_samples:
        print("[FAIL] No frames were presented during the monitoring window!")
        return False

    # Extract sync deltas
    deltas = [s["av_sync_delta_ms"] for s in active_samples]

    # Filter out initial stabilization (first 2 seconds)
    stabilized_samples = [s for s in active_samples if s["elapsed"] > 2.0]
    if len(stabilized_samples) < 5:
        print("[FAIL] Not enough stabilized samples (need at least 5 after 2s warmup)")
        return False

    stabilized_deltas = [s["av_sync_delta_ms"] for s in stabilized_samples]

    # Statistics
    mean_delta = statistics.mean(stabilized_deltas)
    median_delta = statistics.median(stabilized_deltas)
    stdev_delta = statistics.stdev(stabilized_deltas) if len(stabilized_deltas) > 1 else 0.0
    max_abs_delta = max(abs(d) for d in stabilized_deltas)
    min_delta = min(stabilized_deltas)
    max_delta = max(stabilized_deltas)

    # Frame stats from last sample
    last = active_samples[-1]
    total_presented = last["frames_presented"]
    total_dropped = last["frames_dropped"]
    total_held = last["frames_held"]
    drop_ratio = total_dropped / max(total_presented, 1)

    # Stream offset
    first_v = active_samples[0]["first_video_pts"]
    first_a = active_samples[0]["first_audio_pts"]
    pts_offset = abs(first_v - first_a) if first_v >= 0 and first_a >= 0 else -1.0

    # Out-of-tolerance count
    out_of_tolerance = sum(1 for d in stabilized_deltas if abs(d) > MAX_AV_SYNC_DELTA_MS)
    tolerance_pct = (1.0 - out_of_tolerance / len(stabilized_deltas)) * 100.0

    print(f"\n  Stream PTS Analysis:")
    print(f"    First Video PTS:       {first_v:.6f}s")
    print(f"    First Audio PTS:       {first_a:.6f}s")
    print(f"    Stream PTS Offset:     {pts_offset:.3f}s")

    print(f"\n  A/V Sync Delta (stabilized, {len(stabilized_deltas)} samples):")
    print(f"    Mean:                  {mean_delta:+.1f} ms")
    print(f"    Median:                {median_delta:+.1f} ms")
    print(f"    Std Dev:               {stdev_delta:.1f} ms")
    print(f"    Range:                 [{min_delta:+.1f}, {max_delta:+.1f}] ms")
    print(f"    Max |delta|:           {max_abs_delta:.1f} ms")
    print(f"    In-tolerance (±{MAX_AV_SYNC_DELTA_MS:.0f}ms): {tolerance_pct:.1f}%")

    print(f"\n  Frame Statistics:")
    print(f"    Frames Presented:      {total_presented}")
    print(f"    Frames Dropped:        {total_dropped}")
    print(f"    Frames Held:           {total_held}")
    print(f"    Drop Ratio:            {drop_ratio * 100:.1f}%")

    print(f"\n  Total Samples Collected: {len(samples)}")
    print(f"  Active Samples:          {len(active_samples)}")
    print(f"  Stabilized Samples:      {len(stabilized_samples)}")

    # ── Assertions ────────────────────────────────────────────────────────
    failures = []

    # Assertion 1: Mean sync delta within tolerance
    if abs(mean_delta) > MAX_AV_SYNC_DELTA_MS:
        failures.append(f"Mean A/V sync delta ({mean_delta:+.1f}ms) exceeds ±{MAX_AV_SYNC_DELTA_MS:.0f}ms")

    # Assertion 2: Max sync delta within 2x tolerance (allow occasional spikes)
    if max_abs_delta > MAX_AV_SYNC_DELTA_MS * 2.5:
        failures.append(f"Max |A/V sync delta| ({max_abs_delta:.1f}ms) exceeds {MAX_AV_SYNC_DELTA_MS * 2.5:.0f}ms")

    # Assertion 3: At least 90% of samples within tolerance
    if tolerance_pct < 90.0:
        failures.append(f"Only {tolerance_pct:.1f}% of samples within ±{MAX_AV_SYNC_DELTA_MS:.0f}ms (need ≥90%)")

    # Assertion 4: Frame drop ratio
    if drop_ratio > MAX_FRAME_DROP_RATIO:
        failures.append(f"Frame drop ratio ({drop_ratio * 100:.1f}%) exceeds {MAX_FRAME_DROP_RATIO * 100:.0f}%")

    # Assertion 5: Stream PTS offset reasonable
    if pts_offset > MAX_STREAM_PTS_OFFSET_S:
        failures.append(f"Stream PTS offset ({pts_offset:.3f}s) exceeds {MAX_STREAM_PTS_OFFSET_S:.1f}s")

    # Assertion 6: Both streams detected
    if first_v < 0:
        failures.append("Video stream PTS never set (video decoding may have failed)")
    if first_a < 0:
        failures.append("Audio stream PTS never set (audio decoding may have failed)")

    # Assertion 7: Sync should not drift monotonically (steady drift detection)
    if len(stabilized_deltas) >= 10:
        first_quarter = stabilized_deltas[:len(stabilized_deltas)//4]
        last_quarter = stabilized_deltas[-(len(stabilized_deltas)//4):]
        drift = statistics.mean(last_quarter) - statistics.mean(first_quarter)
        if abs(drift) > MAX_AV_SYNC_DELTA_MS:
            failures.append(f"Monotonic sync drift detected: {drift:+.1f}ms over monitoring period")

    # ── Verdict ───────────────────────────────────────────────────────────
    print("\n" + "=" * 78)
    if not failures:
        print("  ✅ RESULT: YOUTUBE A/V SYNC TEST [PASSED]")
        print("     Audio and video are synchronized within broadcast-quality tolerances.")
    else:
        print(f"  ❌ RESULT: YOUTUBE A/V SYNC TEST [FAILED] ({len(failures)} issue(s))")
        for i, f in enumerate(failures, 1):
            print(f"     {i}. {f}")
    print("=" * 78)

    return len(failures) == 0


def main():
    parser = argparse.ArgumentParser(description="YouTube A/V Sync Diagnostic Test")
    parser.add_argument("--url", default=DEFAULT_YOUTUBE_URL, help="YouTube video URL to test")
    args = parser.parse_args()

    passed = run_av_sync_diagnostic_test(args.url)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
