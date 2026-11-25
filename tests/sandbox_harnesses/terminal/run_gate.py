"""Run the Windows hardware Gate 4 test from a fresh output directory."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid


def main():
    project = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=project.parents[2] / "bin/godot.windows.editor.x86_64.exe")
    options = parser.parse_args()
    engine = options.engine.resolve(strict=True)
    runs = project / "runs"
    runs.mkdir(exist_ok=True)
    (runs / ".gdignore").touch()
    output = runs / ("gate4-" + uuid.uuid4().hex)
    output.mkdir()
    print(f"Gate 4 evidence in {output}", flush=True)
    env = os.environ.copy()
    for variable in ("APPDATA", "LOCALAPPDATA", "TEMP", "TMP"):
        directory = output / "cache" / variable
        directory.mkdir(parents=True)
        env[variable] = str(directory)
    args = [engine, "--path", project, "--rendering-method", "forward_plus", "--rendering-driver", "vulkan",
            "--audio-driver", "Dummy", "--max-fps", "60", "--quit-after", "1100", "--", output.as_posix()]
    with (output / "runtime.log").open("wb") as log:
        rendered = subprocess.run(args, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    if not (output / "room.json").is_file():
        raise SystemExit(f"The room did not finish. See {output / 'runtime.log'}")
    ocr = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                          project / "recognize.ps1", "-RunDirectory", output], timeout=240)
    room = json.loads((output / "room.json").read_text(encoding="utf-8"))
    recognized = json.loads((output / "recognition.json").read_text(encoding="utf-8-sig"))
    digest = hashlib.sha256()
    with engine.open("rb") as executable:
        for chunk in iter(lambda: executable.read(1024 * 1024), b""):
            digest.update(chunk)
    report = {key: value for key, value in room.items() if key != "observations"}
    passed = rendered.returncode == ocr.returncode == 0 and not room["failures"] and len(room["observations"]) == 64
    report.update({"engine_sha256": digest.hexdigest(), "ocr": recognized, "passed": passed})
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Gate 4 {'passed' if report['passed'] else 'failed'}. Errors: {room['failures']}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
