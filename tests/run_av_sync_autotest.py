import time
import json
import urllib.request
import subprocess
import os

EXPECTED_SCHEDULE = [5.000, 9.000, 14.000, 20.000, 27.000]

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

def run_test():
    if not is_rouen_running():
        print("[CRASH DETECTED] Rouen process is NOT running at test start!")
        print("Last log lines:\n" + get_last_log_lines())
        return

    url = "http://127.0.0.1:8081/api/cards"
    payload = {
        "uri": "media:/Users/ignaciorodriguez/src/rouen/resources/av_sync_test.mp4"
    }
    req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), headers={'Content-Type': 'application/json'})
    
    print("[AUTOTEST] Triggering Media Card playback via API...")
    try:
        with urllib.request.urlopen(req) as resp:
            print("[AUTOTEST] Card response:", resp.read().decode('utf-8'))
    except Exception as e:
        print(f"[AUTOTEST] Error triggering card: {e}")
        if not is_rouen_running():
            print("\n[CRASH DETECTED] Rouen crashed while handling API request!")
            print("Last log lines:\n" + get_last_log_lines())
        return

    flashes = []
    chirps = []
    
    start_wall = time.time()
    print("[AUTOTEST] Monitoring playback diagnostic stream for 30 seconds...")

    prev_flash = False
    prev_chirp = False
    max_vu_seen = 0.0
    poll_count = 0

    while time.time() - start_wall < 30.0:
        if not is_rouen_running():
            print("\n[CRASH DETECTED] Rouen process crashed during media playback!")
            print("Last log lines:\n" + get_last_log_lines())
            return

        try:
            with urllib.request.urlopen("http://127.0.0.1:8081/api/cast/status") as resp:
                data = json.loads(resp.read().decode('utf-8'))
                pos = data.get("position", 0.0)
                lum = data.get("luminance", 0.0)
                vu_l = data.get("vu_level_l", 0.0)
                poll_count += 1

                if vu_l > max_vu_seen:
                    max_vu_seen = vu_l

                is_flash = lum > 0.3
                is_chirp = vu_l > 0.01

                if is_flash and not prev_flash:
                    flashes.append((pos, lum))
                    print(f"  [FLASH DETECTED] pos={pos:.3f}s lum={lum:.4f}")

                if is_chirp and not prev_chirp:
                    chirps.append((pos, vu_l))
                    print(f"  [AUDIO CHIRP DETECTED] pos={pos:.3f}s vu_l={vu_l:.6f}")

                prev_flash = is_flash
                prev_chirp = is_chirp
        except Exception as e:
            pass

        time.sleep(0.03)

    print(f"\n[AUTOTEST] Total polls: {poll_count} | Peak VU level detected: {max_vu_seen:.6f}")
    print("\n" + "="*72)
    print("           AUTOMATED A/V SYNC CALIBRATION ACCURACY REPORT")
    print("="*72)
    print(f" Expected Calibration Pulse Timestamps (s): {EXPECTED_SCHEDULE}")
    print(f" Total Video Flashes Detected: {len(flashes)}")
    print(f" Total Audio Chirps Detected:  {len(chirps)}")
    print("-" * 72)
    print(f"{'Pulse':<7} | {'Expected':<10} | {'Audio Time':<12} | {'Video Time':<12} | {'A/V Delta':<10} | {'Status'}")
    print("-" * 72)

    all_passed = True
    matched_count = 0

    for idx, expected in enumerate(EXPECTED_SCHEDULE):
        c_match = next((pos for pos, _ in chirps if abs(pos - expected) < 1.5), None)
        f_match = next((pos for pos, _ in flashes if abs(pos - expected) < 1.5), None)

        c_str = f"{c_match:.3f}s" if c_match is not None else "N/A"
        f_str = f"{f_match:.3f}s" if f_match is not None else "N/A"

        if c_match is not None and f_match is not None:
            delta_ms = (f_match - c_match) * 1000.0
            delta_str = f"{delta_ms:+.1f} ms"
            is_ok = abs(delta_ms) <= 150.0
            status = "PASS" if is_ok else "FAIL"
            if is_ok:
                matched_count += 1
            else:
                all_passed = False
        else:
            delta_str = "N/A"
            status = "MISSING"

        print(f"#{idx+1:<6} | {expected:<10.3f} | {c_str:<12} | {f_str:<12} | {delta_str:<10} | [{status}]")

    print("="*72)
    if matched_count == len(EXPECTED_SCHEDULE) and all_passed:
        print(" SUMMARY RESULT: 100% PERFECT A/V SYNCHRONIZATION VERIFIED [PASSED]")
    else:
        print(" SUMMARY RESULT: A/V SYNC MISALIGNMENT DETECTED [FAILED]")
    print("="*72)

if __name__ == '__main__':
    run_test()
