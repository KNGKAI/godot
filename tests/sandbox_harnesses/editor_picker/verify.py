"""Drive the picker in a fresh Godot editor project and record native Inspector and animation edits."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editor", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    editor = args.editor.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=True)
    run = args.output.resolve() / ("picker-" + uuid.uuid4().hex[:8])
    run.mkdir()
    print(run, flush=True)
    project = run / "project"
    shutil.copytree(
        Path(__file__).parent,
        project,
        ignore=shutil.ignore_patterns(".godot", "__pycache__", "*.py", "*.md", "results"),
    )
    env = os.environ.copy()
    for key in ("APPDATA", "LOCALAPPDATA", "TEMP", "TMP", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME"):
        value = run / key
        value.mkdir()
        env[key] = str(value)
    probe = run / "observations.json"
    common = [str(editor), "--headless", "--editor", "--path", str(project), "--resolution", "1600x1000", "--language", "en"]
    for stage in ("import", "editor"):
        command = common + (["--import"] if stage == "import" else ["res://world.tscn"])
        if stage == "editor":
            env["SANDBOX_PICKER_REPORT"] = str(probe)
        log = run / f"{stage}.log"
        with log.open("wb") as stream:
            process = subprocess.run(command, cwd=run, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=100)
        if process.returncode:
            raise SystemExit(f"{stage} exited {process.returncode}. See {log}")
    data = json.loads(probe.read_text(encoding="utf-8"))
    required = {"typed state selection", "fuzzy namespace results", "missing ID badge", "type mismatch badge",
                "native timeline key Inspector", "timeline action and sibling payload preserved", "frame marker selects target"}
    required.update(f"{target} value changed" for target in ("Inspector actor", "Inspector marker", "Timeline actor", "Timeline marker"))
    if not all(data["checks"].values()) or not required.issubset(data["checks"]):
        raise SystemExit(f"Picker verification failed. See {probe}")
    with editor.open("rb") as binary:
        data["editor_sha256"] = hashlib.file_digest(binary, "sha256").hexdigest()
    (run / "result.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"{len(data['checks'])} checks passed. See {run / 'result.json'}")


if __name__ == "__main__":
    main()
