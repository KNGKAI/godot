"""Run the Gate 8 native graph authoring probe with an isolated editor profile."""

import argparse
import os
import shutil
import subprocess
import uuid
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editor", type=Path)
    parser.add_argument("output", type=Path, help="Directory for the probe's editor log")
    args = parser.parse_args()
    editor = args.editor.resolve()
    source = Path(__file__).resolve().parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work = output / ("graph-project-" + uuid.uuid4().hex[:10])
    work.mkdir()
    project = work / "project"
    project.mkdir()
    shutil.copy2(source / "project.godot", project)
    shutil.copytree(source / "addons", project / "addons")
    env = os.environ.copy()
    for key in ("APPDATA", "LOCALAPPDATA", "TMP", "TEMP", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME"):
        profile = work / key.lower()
        profile.mkdir()
        env[key] = str(profile)
    log = output / "graph-authoring.log"
    with log.open("wb") as stream:
        result = subprocess.run(
            [str(editor), "--headless", "--editor", "--path", str(project), "--language", "en", "--", "--graph-probe"],
            env=env,
            stdout=stream,
            stderr=subprocess.STDOUT,
            timeout=180,
        )
    text = log.read_text(encoding="utf-8", errors="replace")
    print(text)
    if result.returncode or "Gate 8 native graph authoring: checks=" not in text or "failed=false" not in text:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
