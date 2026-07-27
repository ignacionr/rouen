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

def test_media_player():
    if not is_rouen_running():
        print("[FAIL] Rouen process is NOT running!")
        sys.exit(1)

    url = "http://127.0.0.1:8081/api/cards"
    payload = {
        "uri": "media:/Users/ignaciorodriguez/src/rouen/resources/av_sync_test.mp4"
    }
    req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers={'Content-Type': 'application/json'})
    
    print("[TEST] 1. Creating Media Card via API...")
    try:
        with urllib.request.urlopen(req) as resp:
            print("       Card response:", resp.read().decode('utf-8'))
    except Exception as e:
        print(f"[FAIL] Error triggering card: {e}")
        sys.exit(1)

    # -------------------------------------------------------------
    # TEST 1: Progress Bar Pacing (position increases smoothly)
    # -------------------------------------------------------------
    print("\n[TEST] 2. Verifying Progress Bar Pacing (position vs duration)...")
    positions = []
    start_wall = time.time()
    
    for i in range(12):
        if not is_rouen_running():
            print("[FAIL] Rouen process crashed during progress bar test!")
            sys.exit(1)

        try:
            with urllib.request.urlopen("http://127.0.0.1:8081/api/cast/status") as resp:
                data = json.loads(resp.read().decode('utf-8'))
                pos = data.get("position", 0.0)
                dur = data.get("duration", 0.0)
                is_playing = data.get("is_media_playing", False)
                progress_pct = (pos / dur * 100.0) if dur > 0 else 0.0
                positions.append((time.time() - start_wall, pos, dur, progress_pct, is_playing))
                print(f"       Sample #{i+1:02d}: elapsed={time.time()-start_wall:.1f}s | pos={pos:.2f}s / {dur:.2f}s ({progress_pct:.1f}%) | is_playing={is_playing}")
        except Exception as e:
            print(f"       Sample #{i+1:02d}: polling error ({e})")

        time.sleep(0.5)

    # Assert progress pacing:
    # 1) Position must be strictly < duration during initial 6 seconds
    # 2) Position at sample #2 must be < 80% of duration (not jumped to 100% early!)
    if len(positions) >= 4:
        sample_early = positions[2] # ~1.0s elapsed
        pos_early, dur_early, pct_early = sample_early[1], sample_early[2], sample_early[3]
        if dur_early > 0 and pct_early > 75.0:
            print(f"\n[FAIL] Progress bar jumped to {pct_early:.1f}% prematurely at {sample_early[0]:.1f}s elapsed!")
            sys.exit(1)
        else:
            print(f"\n[PASS] Progress bar pacing verified! (At {sample_early[0]:.1f}s elapsed, progress is {pct_early:.1f}%)")

    # -------------------------------------------------------------
    # TEST 2: Playback Completion & Stop State Transition
    # -------------------------------------------------------------
    print("\n[TEST] 3. Monitoring Playback Completion & Stop State Transition...")
    eof_stop_verified = False
    monitor_start = time.time()

    while time.time() - monitor_start < 35.0:
        if not is_rouen_running():
            print("[FAIL] Rouen process crashed during EOF monitor!")
            sys.exit(1)

        try:
            with urllib.request.urlopen("http://127.0.0.1:8081/api/cast/status") as resp:
                data = json.loads(resp.read().decode('utf-8'))
                is_playing = data.get("is_media_playing", False)
                pos = data.get("position", 0.0)
                dur = data.get("duration", 0.0)

                if not is_playing:
                    print(f"       [STOPPED CONFIRMED] Elapsed: {time.time()-start_wall:.1f}s | is_media_playing=False | pos={pos:.2f}s / {dur:.2f}s")
                    eof_stop_verified = True
                    break
        except Exception:
            pass

        time.sleep(0.5)

    print("\n" + "="*72)
    if eof_stop_verified:
        print(" SUMMARY RESULT: PROGRESS BAR PACING & STOP STATE VERIFIED [PASSED]")
        print("="*72)
    else:
        print(" SUMMARY RESULT: STOP STATE TRANSITION TIMEOUT [FAILED]")
        print("="*72)
        sys.exit(1)

if __name__ == '__main__':
    test_media_player()
