"""Join a warmed Gate 6 Vulkan heap log, per-frame positions and Windows GPU counters."""

import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path


HEAP_EVENT = re.compile(r"GATE6_HEAPS usec=(\d+) device_local=(\d+) nonlocal=(\d+) total=(\d+)")
HEAP_NAMES = ("device_local_bytes", "nonlocal_bytes", "total_bytes")


def heap_events(path):
    events = []
    with path.open(encoding="utf-8") as log:
        for line in log:
            match = HEAP_EVENT.search(line)
            if match:
                tick, device, host, total = map(int, match.groups())
                if device + host != total:
                    raise ValueError(f"Inconsistent Vulkan totals at tick {tick}")
                events.append((tick, dict(zip(HEAP_NAMES, (device, host, total)))))
    if not events:
        raise ValueError(f"No Vulkan allocation events in {path}")
    return sorted(events, key=lambda event: event[0])


def heap_at(events, tick):
    return next(value for stamp, value in reversed(events) if stamp <= tick)


def dense_interval(path, start):
    tick = start
    first = last = None
    with path.open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            tick += int(row["wall_interval_us"])
            if 4000 <= float(row["world_x_m"]) < 11500:
                if first is None:
                    first = tick
                last = tick
    if first is None or last is None or last <= first:
        raise ValueError("The frame log does not cover the urban route")
    return first, last


def wddm_measurements(path, pid, luid, windows, dense_utc):
    readings = []
    with path.open(newline="", encoding="utf-8-sig") as file:
        for row in csv.DictReader(file):
            parts = row["adapter_instance"].lower().split("_")
            if parts[:2] == ["pid", str(pid)] and int(parts[4], 16) == luid:
                readings.append((int(row["unix_ms"]) / 1000, int(row["dedicated_bytes"]), int(row["shared_bytes"])))
    if not readings:
        raise ValueError("No WDDM counters for the selected renderer LUID and benchmark PID")
    result = {"adapter_luid": hex(luid), "samples": len(readings)}
    for name, window in windows.items():
        observed = window["observed_unix_s"]
        start = max(window["from_unix_s"], observed - 2)
        settled = [row for row in readings if start <= row[0] <= observed]
        result[name] = {"samples": len(settled)}
        if settled:
            for column, label in enumerate(("dedicated_bytes", "shared_bytes"), start=1):
                values = [row[column] for row in settled]
                result[name][label] = {"min": min(values), "max": max(values), "last": values[-1]}
    dense = [row for row in readings if dense_utc[0] <= row[0] <= dense_utc[1]]
    if not dense or any(result[key]["samples"] == 0 for key in ("before", "after")):
        raise ValueError("WDDM sampling did not cover both settled endpoints and the dense route")
    result["dense_peak"] = {
        "dedicated_bytes": max(row[1] for row in dense),
        "shared_bytes": max(row[2] for row in dense),
    }
    return result


def verify_grid(path, expected_hash):
    manifest = path.read_bytes()
    if hashlib.sha256(manifest).hexdigest() != expected_hash:
        return False
    text = manifest.decode("utf-8")
    coords = re.findall(r"^grid_coords = Vector2i\((\d+), (\d+)\)$", text, re.MULTILINE)
    expected = {(str(x), str(z)) for x in range(64) for z in range(64)}
    return len(coords) == 4096 and set(coords) == expected and "cell_size = 250.0" in text


def verify_timing_frames(path, timing):
    intervals = []
    activation = []
    with path.open(newline="", encoding="utf-8") as file:
        for row in csv.DictReader(file):
            intervals.append(int(row["wall_interval_us"]))
            activation.append(int(row["activation_us"]))
    if not intervals or len(intervals) != timing["frames"]:
        return False
    intervals.sort()
    return (
        intervals[math.ceil(0.99 * len(intervals)) - 1] / 1000 == timing["p99_frame_ms"]
        and max(activation) / 1000 == timing["max_activation_ms"]
        and sum(value > 2000 for value in activation) == timing["frames_activation_over_2ms"]
    )


def analyze(args):
    result_dir = args.results
    memory = json.loads((result_dir / "memory.json").read_text(encoding="utf-8"))
    timing = json.loads((result_dir / "timing.json").read_text(encoding="utf-8"))
    visual = json.loads((result_dir / "visual.json").read_text(encoding="utf-8"))
    configs = [run["config"] for run in (memory, timing, visual)]
    if any(len({config[key] for config in configs}) != 1 for key in ("manifest_sha256", "gpu")):
        raise ValueError("The timing and heap runs used different worlds or adapters")
    if timing["config"]["executable_sha256"] != visual["config"]["executable_sha256"]:
        raise ValueError("The clean timing and visual runs used different engine binaries")
    windows = memory["settled_windows"]
    entry = windows["before"]
    first, last = dense_interval(result_dir / "memory-frames.csv", entry["observed_tick_us"])
    offset_seconds = entry["observed_unix_s"] - entry["observed_tick_us"] / 1_000_000
    dense_utc = (offset_seconds + first / 1_000_000, offset_seconds + last / 1_000_000)
    events = heap_events(result_dir / "heaps.log")
    heaps = {key: heap_at(events, window["observed_tick_us"]) for key, window in windows.items()}
    for key in ("before", "after"):
        if heaps[key]["total_bytes"] != memory[key]["video_bytes"]:
            raise ValueError(f"Heap and engine observations differ at the {key} endpoint")
    dense_heaps = [heap_at(events, first)] + [value for tick, value in events if first < tick <= last]
    heaps["dense_peak"] = {key: max(value[key] for value in dense_heaps) for key in HEAP_NAMES}
    wddm = wddm_measurements(result_dir / "wddm.csv", memory["config"]["pid"], args.adapter_luid, windows, dense_utc)
    before, after = heaps["before"], heaps["after"]
    analysis = {
        "gpu": memory["config"]["gpu"],
        "manifest_sha256": memory["config"]["manifest_sha256"],
        "timing_executable_sha256": timing["config"]["executable_sha256"],
        "memory_executable_sha256": memory["config"]["executable_sha256"],
        "clean_timing": {
            "frames": timing["frames"],
            "p99_frame_ms": timing["p99_frame_ms"],
            "max_activation_ms": timing["max_activation_ms"],
            "frames_activation_over_2ms": timing["frames_activation_over_2ms"],
            "before_all_heap_bytes": timing["before"]["video_bytes"],
            "peak_dense_all_heap_bytes": timing["peak_dense_video_bytes"],
            "after_all_heap_bytes": timing["after"]["video_bytes"],
            "wall_average_m_s": timing["distance_m"] / timing["elapsed_s"],
            "physics_speed_m_s": timing["physics_speed_m_s"],
            "origin_shifts_x_m": timing["origin_shifts_x_m"],
        },
        "diagnostic_timing": {
            key: memory[key] for key in ("passed", "frames_activation_over_2ms", "p99_frame_ms", "max_activation_ms")
        },
        "visual_observations": {
            key: visual[key] for key in (
                "readbacks", "visible_fade_frames", "terrain_pixels_checked", "marker_samples",
                "failed_visual_frames", "shift_window_frames", "failed_shift_frames",
            )
        },
        "vulkan_heap_allocations": heaps,
        "wddm_process_memory": wddm,
        "checks": {
            "device_local_rose_in_town": heaps["dense_peak"]["device_local_bytes"] > before["device_local_bytes"],
            "device_local_returned_exactly": after["device_local_bytes"] == before["device_local_bytes"],
            "matching_scene_batches": (
                bool(memory["before_scene_batches"]) and memory["before_scene_batches"] == memory["after_scene_batches"]
            ),
            "all_town_atlases_released": memory["dense_textures_still_cached"] == 0,
            "texture_bytes_equal": memory["before"]["texture_bytes"] == memory["after"]["texture_bytes"],
            "buffer_bytes_equal": memory["before"]["buffer_bytes"] == memory["after"]["buffer_bytes"],
            "diagnostic_all_heap_returned_exactly": before["total_bytes"] == after["total_bytes"],
            "wddm_dedicated_returned_exactly": (
                wddm["before"]["dedicated_bytes"]["last"] == wddm["after"]["dedicated_bytes"]["last"]
            ),
            "clean_p99_within_16_6_ms": timing["p99_frame_ms"] <= 16.6,
            "clean_activation_within_2_ms": timing["max_activation_ms"] <= 2.0,
            "clean_all_heap_returned_exactly": timing["before"]["video_bytes"] == timing["after"]["video_bytes"],
            "clean_run_reported_pass": timing["passed"],
            "visual_run_reported_pass": visual["passed"],
        },
    }
    raw_checks = analysis["checks"]
    required = {
        "complete_16_km_square": verify_grid(args.manifest, analysis["manifest_sha256"]),
        "complete_route_at_90_m_s": all(
            run["distance_m"] == 15550.0 and run["physics_speed_m_s"] == 90.0
            for run in (timing, visual, memory)
        ),
        "at_least_four_origin_shifts": all(
            len(set(run["origin_shifts_x_m"])) >= 4 for run in (timing, visual, memory)
        ),
        "ordinary_frame_log_verified": verify_timing_frames(result_dir / "timing-frames.csv", timing),
        "no_ordinary_activation_overruns": timing["frames_activation_over_2ms"] == 0,
        "every_visual_frame_checked": visual["readbacks"] == visual["frames"] and visual["readbacks"] > 0,
        "active_visible_transitions": visual["visible_fade_frames"] > 0 and visual["clear_color_positive_control"],
        "no_rendered_gaps": visual["frames_with_terrain_holes"] == 0 and visual["failed_visual_frames"] == 0,
        "no_rendered_shift_errors": (
            visual["shift_window_frames"] >= len(visual["origin_shifts_x_m"]) * 4 and visual["failed_shift_frames"] == 0
        ),
    }
    for key in (
        "device_local_rose_in_town", "device_local_returned_exactly", "matching_scene_batches",
        "all_town_atlases_released", "texture_bytes_equal", "buffer_bytes_equal", "wddm_dedicated_returned_exactly",
        "clean_p99_within_16_6_ms", "clean_activation_within_2_ms", "visual_run_reported_pass",
    ):
        required[key] = raw_checks[key]
    analysis["required_checks"] = required
    analysis["gate6_passed"] = all(required.values())
    return analysis


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    project = Path(__file__).resolve().parent
    parser.add_argument("--results", type=Path, default=project / "results")
    parser.add_argument("--manifest", type=Path, default=project / "world" / "manifest.tres")
    parser.add_argument(
        "--adapter-luid",
        required=True,
        type=lambda value: int(value, 0),
        help="Low DWORD of the Vulkan adapter LUID, e.g. 0xe761",
    )
    args = parser.parse_args()
    try:
        analysis = analyze(args)
    except (OSError, ValueError, KeyError, IndexError, StopIteration) as exc:
        analysis = {"gate6_passed": False, "missing_or_invalid_evidence": str(exc)}
    output = json.dumps(analysis, indent=2)
    if args.results.is_dir():
        (args.results / "memory-analysis.json").write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0 if analysis["gate6_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
