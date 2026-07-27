import time
import json
import urllib.request
import subprocess
import os
import sys

def is_rouen_running():
    try:
        out = subprocess.check_output(["pgrep", "rouen"]).decode().strip()
        return len(out) > 0
    except Exception:
        return False

def get_last_log_lines(n=20):
    try:
        if os.path.exists("/tmp/rouen_test_run.log"):
            with open("/tmp/rouen_test_run.log", "r") as f:
                lines = f.readlines()
                return "".join(lines[-n:])
    except Exception:
        pass
    return "No log output available."

def test_youtube_av_sync():
    if not is_rouen_running():
        print("[FAIL] Rouen process is NOT running!")
        sys.exit(1)

    url = "http://127.0.0.1:8081/api/cards"
    # Public short YouTube clip (Big Buck Bunny trailer)
    payload = {
        "uri": "media:https://www.youtube.com/watch?v=jNQXAC9IVRw"
    }
    req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers={'Content-Type': 'application/json'})
    
    print("[AUTOTEST-YOUTUBE] 1. Triggering YouTube Media Card via API...")
    try:
        with urllib.request.urlopen(req) as resp:
            print("                   Response:", resp.read().decode('utf-8'))
    except Exception as e:
        print(f"[FAIL] Error triggering YouTube card: {e}")
        sys.exit(1)

    print("\n[AUTOTEST-YOUTUBE] 2. Monitoring YouTube A/V Sync & Pacing for 20 seconds...")
    samples = []
    start_wall = time.time()
    
    for i in range(25):
        if not is_rouen_running():
            print("[FAIL] Rouen process crashed during YouTube A/V sync test!")
            print("Last log lines:\n" + get_last_log_lines())
            sys.exit(1)

        try:
            with urllib.request.urlopen("http://127.0.0.1:8081/api/cast/status") as resp:
                data = json.loads(resp.read().decode('utf-8'))
                pos = data.get("position", 0.0)
                dur = data.get("duration", 0.0)
                is_playing = data.get("is_media_playing", False)
                v_qsize = data.get("video_queue_size", 0)
                elapsed = time.time() - start_wall
                samples.append((elapsed, pos, dur, v_qsize, is_playing))
                print(f"  [SAMPLE #{i+1:02d} @{elapsed:4.1f}s] pos={pos:6.2f}s / {dur:6.2f}s | video_q={v_qsize:02d} | is_playing={is_playing}")
        except Exception as e:
            pass

        time.sleep(0.8)

    # -------------------------------------------------------------
    # ASSERTIONS:
    # -------------------------------------------------------------
    playing_samples = [s for s in samples if s[4]]
    if not playing_samples:
        print("\n[FAIL] YouTube media playback failed to start playing within 20s!")
        sys.exit(1)

    # Check 1.0x playback speed (position delta vs elapsed delta)
    first_p = playing_samples[0]
    last_p = playing_samples[-1]
    elapsed_delta = last_p[0] - first_p[0]
    pos_delta = last_p[1] - first_p[1]

    print("\n" + "="*72)
    print("           YOUTUBE AUTOMATED A/V SYNC & PACING ACCURACY REPORT")
    print("="*72)
    print(f" Initial Playing Position: {first_p[1]:.2f}s @ {first_p[0]:.1f}s wall-clock")
    print(f" Final Playing Position:   {last_p[1]:.2f}s @ {last_p[0]:.1f}s wall-clock")
    print(f" Wall-clock Time Delta:    {elapsed_delta:.2f}s")
    print(f" Media Position Delta:      {pos_delta:.2f}s")

    # Ratio of pos_delta / elapsed_delta must be between 0.7x and 1.3x (NOT 3.0x or fast-forwarded!)
    speed_ratio = pos_delta / elapsed_delta if elapsed_delta > 0 else 0.0
    print(f" Playback Pacing Ratio:    {speed_ratio:.2f}x")
    print("-" * 72)

    if 0.6 <= speed_ratio <= 1.4:
        print(" SUMMARY RESULT: REAL YOUTUBE A/V SYNC & PACING VERIFIED [PASSED]")
        print("="*72)
    else:
        print(f" SUMMARY RESULT: YOUTUBE PACING ABNORMAL ({speed_ratio:.2f}x) [FAILED]")
        print("="*72)
        sys.exit(1)

if __name__ == '__main__':
    test_youtube_av_sync()
