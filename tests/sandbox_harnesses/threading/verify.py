"""Build and run the Gate 2 doctest with a Linux GCC sanitizer from a Godot source tree."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile


MODULES = ("sandbox_core", "sandbox_sim", "sandbox_narrative", "sandbox_streaming", "text_server_fb", "websocket")
DISABLED_OPTIONS = (
    "vulkan", "opengl3", "rendering_device", "sdl", "accesskit", "x11", "wayland",
    "alsa", "pulseaudio", "dbus", "speechd", "fontconfig", "udev",
)
SOURCE_PAIRS = (
    "modules/sandbox_sim/simulation_server",
    "modules/sandbox_sim/sim_module",
    "modules/sandbox_core/event_bus",
    "modules/sandbox_core/world_state",
    "core/object/worker_thread_pool",
)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sanitizer", choices=["asan", "tsan"])
    parser.add_argument("output", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    build = [
        sys.executable,
        "-m",
        "SCons",
        f"-j{args.jobs}",
        "platform=linuxbsd",
        "target=template_debug",
        "arch=x86_64",
        "tests=yes",
        "threads=yes",
        f"use_{args.sanitizer}=yes",
        "use_static_cpp=no",
        "optimize=none",
        "debug_symbols=no",
        "scu_build=yes",
        "scu_limit=5",
        "modules_enabled_by_default=no",
    ]
    build.extend(f"module_{module}_enabled=yes" for module in MODULES)
    build.extend(f"{option}=no" for option in DISABLED_OPTIONS)
    with (output / "build.log").open("wb") as log:
        subprocess.run(build, stdout=log, stderr=subprocess.STDOUT, check=True)

    engine = Path("bin/godot.linuxbsd.template_debug.x86_64.san").resolve(strict=True)
    dynamic = subprocess.check_output(["readelf", "-d", engine], text=True)
    runtime = re.search(rf"\(NEEDED\).*\[(lib{args.sanitizer}\.so[^\]]*)\]", dynamic)
    if not runtime:
        raise RuntimeError(f"Missing {args.sanitizer} runtime dependency in {engine}")
    options = {
        "asan": {"ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1"},
        "tsan": {"TSAN_OPTIONS": "halt_on_error=1"},
    }
    command = [str(engine), "--headless", "--test", "--test-case=*[Gate2]*", "--success", "--no-colors"]
    with tempfile.TemporaryDirectory(prefix="godot-gate2-user-") as data_dir:
        env = os.environ.copy()
        for name in ("ASAN_OPTIONS", "LSAN_OPTIONS", "TSAN_OPTIONS"):
            env.pop(name, None)
        env.update(options[args.sanitizer], XDG_DATA_HOME=data_dir, XDG_CONFIG_HOME=data_dir, XDG_CACHE_HOME=data_dir)
        run = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / "doctest.txt").write_text(run.stdout, encoding="utf-8")

    totals = re.search(r"\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*0 failed", run.stdout)
    passed = (
        run.returncode == 0
        and re.search(r"\[doctest\] test cases:\s*1\s*\|\s*1 passed\s*\|\s*0 failed", run.stdout)
        and totals
        and int(totals[1]) == int(totals[2]) > 0
        and "expected_overlap := 8" in run.stdout
        and "80000 == 80000" in run.stdout
        and not re.search(r"(?m)^(?:WARNING|ERROR|FATAL):", run.stdout)
    )
    sources = [Path("modules/sandbox_sim/tests/test_simulation_server.h")]
    for stem in SOURCE_PAIRS:
        sources.extend([Path(stem + ".h"), Path(stem + ".cpp")])
    summary = {
        "passed": bool(passed),
        "utc": datetime.now(timezone.utc).isoformat(),
        "kernel": platform.release(),
        "compiler": subprocess.check_output(["g++", "--version"], text=True).splitlines()[0],
        "scons": subprocess.check_output([sys.executable, "-m", "SCons", "--version"], text=True).splitlines()[1].strip(),
        "build": build,
        "run": command,
        "options": options[args.sanitizer],
        "sanitizer_library": runtime[1],
        "engine_sha256": sha256(engine),
        "source_sha256": {str(path): sha256(path) for path in sources},
        "run_exit": run.returncode,
        "passed_assertions": int(totals[2]) if totals else None,
    }
    (output / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if not passed:
        sys.exit(f"Gate 2 failed. See {output / 'doctest.txt'}")
    print(f"Gate 2 passed under {runtime[1]} with {totals[2]} assertions. Evidence in {output}")


if __name__ == "__main__":
    main()
