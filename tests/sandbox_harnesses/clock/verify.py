"""Measure Gate 1 on Windows with Jolt, the active audio output and a rendered terminal."""

import argparse
import ctypes
import hashlib
import json
import os
import queue
import socket
import subprocess
import sys
import threading
import time
import uuid
from ctypes import wintypes
from datetime import datetime, timezone
from pathlib import Path

PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[2]
KEYS = "abcdefghijklmnopqrstuvwxyz" * 2


def engine_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as executable:
        for chunk in iter(lambda: executable.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_output(stream, log, events):
    with log.open("w", encoding="utf-8") as file:
        for line in stream:
            received_ns = time.perf_counter_ns()
            file.write(line)
            if line.startswith("GATE1 "):
                events.put((received_ns, json.loads(line[6:])))
    events.put((0, {"event": "eof"}))


def receive(events, name):
    timestamp, event = events.get(timeout=60)
    if event["event"] != name:
        raise RuntimeError(f"Expected {name}, received {event}")
    return timestamp, event


def acknowledgement(feedback, name):
    data = feedback.recv(4096)
    timestamp = time.perf_counter_ns()
    event = json.loads(data)
    if event["event"] != name:
        raise RuntimeError(f"Expected {name}, received {event}")
    return timestamp, event


def type_keys(hwnd, pid, feedback):
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.PostMessageW.argtypes = (wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    user32.PostMessageW.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    owner = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
    if owner.value != pid:
        raise RuntimeError("The target window does not belong to the probe.")
    samples = []
    for index, char in enumerate(KEYS):
        # Vary the offset relative to frame polling. Only the probe's own window receives input.
        time.sleep((31 + 17 * index % 79) / 1000)
        vk = ord(char.upper())
        key_data = 1 | (user32.MapVirtualKeyW(vk, 0) << 16)
        start_ns = time.perf_counter_ns()
        if not user32.PostMessageW(hwnd, 0x100, vk, key_data):
            raise ctypes.WinError(ctypes.get_last_error())
        edit_ns, edit = acknowledgement(feedback, "edited")
        draw_ns, draw = acknowledgement(feedback, "drawn")
        # Let Windows translate the key down into WM_CHAR before queuing the release.
        if not user32.PostMessageW(hwnd, 0x101, vk, key_data | 0xC0000000):
            raise ctypes.WinError(ctypes.get_last_error())
        if edit["text"] != KEYS[: index + 1] or draw["text"] != edit["text"]:
            raise RuntimeError(f"Incorrect terminal edit at key {index + 1}: {edit}, {draw}")
        samples.append({
            "key": char,
            "frame": draw["frame"],
            "edit_ms": (edit_ns - start_ns) / 1e6,
            "post_draw_ms": (draw_ns - start_ns) / 1e6,
        })
    return samples


def measure(engine, directory, mode, env, feedback):
    args = [
        engine, "--path", PROJECT, "--rendering-method", "forward_plus", "--rendering-driver", "vulkan",
        "--max-fps", "240", "--", mode, str(feedback.getsockname()[1]),
    ]
    events = queue.Queue()
    with subprocess.Popen(
        args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace"
    ) as process:
        reader = threading.Thread(target=read_output, args=(process.stdout, directory / f"{mode}.log", events))
        reader.start()
        try:
            _, ready = receive(events, "ready")
            samples = type_keys(ready["window"], process.pid, feedback)
            _, report = receive(events, "finished")
            if process.wait(timeout=10):
                raise RuntimeError("The clock host exited with an error.")
        finally:
            if process.poll() is None:
                process.kill()
            reader.join(timeout=5)

    before, after = report["before"], report["after"]
    elapsed = (after["usec"] - before["usec"]) / 1e6
    sim_ticks = after["module_ticks"] - before["module_ticks"]
    clock_ticks = after["clock_ticks"] - before["clock_ticks"]
    physics_ticks = after["physics_ticks"] - before["physics_ticks"]
    sim_rate = (after["module_seconds"] - before["module_seconds"]) / elapsed
    audio_rate = (after["audio_seconds"] - before["audio_seconds"]) / elapsed
    physics_hz = physics_ticks / elapsed
    motion = after["body_x"] - before["body_x"]
    edit_max = max(s["edit_ms"] for s in samples)
    draw_max = max(s["post_draw_ms"] for s in samples)
    checks = {
        "live_sim_module": clock_ticks == sim_ticks and (
            abs(sim_rate - 50) < 0.5 if mode == "fast" else sim_rate == 0 and sim_ticks == 0),
        "sim_configuration": report["sim_scale"] == (50 if mode == "fast" else 0)
        and report["sim_paused"] == (mode == "paused"),
        "jolt_direct_body": report["body_state"] == "JoltPhysicsDirectBodyState3D",
        "jolt_step_60hz": all(abs(report[key] - 1 / 60) < 1e-7 for key in ("physics_step_min", "physics_step_max")),
        "jolt_wall_rate": abs(physics_ticks - 60 * elapsed) < 2,
        "jolt_motion": abs(motion - physics_ticks / 60) < 0.001,
        "audio_device": report["audio_driver"] != "Dummy",
        "audio_scale": report["audio_scale_min"] == report["audio_scale_max"] == report["player_pitch"] == 1,
        "audio_playhead": report["audio_playing"] and abs(audio_rate - 1) < 0.01,
        "terminal_edits": report["terminal_engaged"] and report["terminal_text"] == KEYS,
        "visible_command_line_changed": report["command_region_changed_pixels"] > 128,
        "distinct_draws": len({s["frame"] for s in samples}) == len(KEYS),
        "terminal_input_under_16_6_ms": edit_max < 16.6,
        "terminal_post_draw_under_16_6_ms": draw_max < 16.6,
    }
    report.update({"elapsed_seconds": elapsed, "sim_seconds_per_second": sim_rate, "physics_hz": physics_hz,
                   "audio_seconds_per_second": audio_rate, "max_edit_ms": edit_max, "max_post_draw_ms": draw_max,
                   "input_samples": samples, "checks": checks})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=REPO / "bin/godot.windows.editor.x86_64.exe")
    options = parser.parse_args()
    engine = options.engine.resolve(strict=True)
    run_id = uuid.uuid4().hex
    directory = REPO / "bin/phase8/clock" / run_id
    directory.mkdir(parents=True)
    print(directory, flush=True)
    env = os.environ.copy()
    for key in ("APPDATA", "LOCALAPPDATA", "TEMP", "TMP"):
        path = directory / key
        path.mkdir()
        env[key] = str(path)
    digest = engine_digest(engine)
    report = {"run_id": run_id, "started_utc": datetime.now(timezone.utc).isoformat(), "engine_sha256": digest,
              "max_fps": 240, "modes": {}}
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as feedback:
            feedback.bind(("127.0.0.1", 0))
            feedback.settimeout(5)
            for mode in ("fast", "paused"):
                report["modes"][mode] = measure(engine, directory, mode, env, feedback)
    finally:
        report["same_executable"] = engine_digest(engine) == digest
        report["passed"] = (
            report["same_executable"]
            and len(report["modes"]) == 2
            and all(all(m["checks"].values()) for m in report["modes"].values())
        )
        (directory / "gate1.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for mode, result in report["modes"].items():
        print(mode, {k: result[k] for k in ("physics_hz", "audio_seconds_per_second", "max_edit_ms", "max_post_draw_ms")})
        print("Failed checks:", [k for k, passed in result["checks"].items() if not passed])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
