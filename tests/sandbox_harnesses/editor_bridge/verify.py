"""Run the editor/game IPC probe or check a real Windows release export in an isolated project."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("check", choices=("bridge", "export"))
    parser.add_argument("editor", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--template", type=Path)
    args = parser.parse_args()
    editor = args.editor.resolve(strict=True)
    run = args.output.resolve() / (args.check + "-" + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    project = run / "project"
    shutil.copytree(Path(__file__).parent, project, ignore=shutil.ignore_patterns(".godot", "__pycache__", "*.py", "results"))
    env = os.environ.copy()
    for var in ("APPDATA", "LOCALAPPDATA", "TEMP", "TMP"):
        directory = run / var
        directory.mkdir()
        env[var] = str(directory)
    report = {"editor_sha256": sha256(editor)}

    def invoke(label, exe, *opts, expected=0, timeout=100, extra=None):
        with (run / (label + ".log")).open("wb") as log:
            result = subprocess.run(
                [str(exe), "--headless", "--path", str(project), *map(str, opts)],
                cwd=run, env=env | (extra or {}), stdout=log, stderr=subprocess.STDOUT, timeout=timeout,
            )
        output = (run / (label + ".log")).read_text(encoding="utf-8", errors="replace")
        if (expected == 0 and result.returncode != 0) or (expected != 0 and result.returncode == 0):
            raise RuntimeError(f"{label} returned {result.returncode}\n{output[-5000:]}")
        return output

    print(run, flush=True)
    invoke("import", editor, "--editor", "--import")
    if args.check == "bridge":
        probe = run / "bridge.json"
        invoke("editor", editor, "--editor", extra={"SANDBOX_BRIDGE_REPORT": str(probe)})
        report.update(json.loads(probe.read_text(encoding="utf-8")))
        if report["errors"] or not all(report["checks"].values()) or len(report["checks"]) < 9:
            raise RuntimeError(report)
    else:
        if not args.template:
            parser.error("--template is required for export")
        template = args.template.resolve(strict=True)
        report["release_template_sha256"] = sha256(template)
        preset = (
            '[preset.0]\nname="Sandbox Windows"\nplatform="Windows Desktop"\nrunnable=true\n'
            'export_filter="all_resources"\ninclude_filter=""\nexclude_filter="addons/*"\n'
            '[preset.0.options]\ncustom_template/release=' + json.dumps(template.as_posix()) +
            '\napplication/modify_resources=false\nbinary_format/embed_pck=false\n'
            'debug/export_console_wrapper=0\n'
        )
        (project / "export_presets.cfg").write_text(preset, encoding="utf-8")
        layout_before = {name: sha256(project / name) for name in ("mission.tres", "sequence.tres")}
        exe = run / "fixture.exe"
        invoke("export-release", editor, "--export-release", "Sandbox Windows", exe)
        package = exe.with_suffix(".pck")
        if not exe.is_file() or not package.is_file():
            raise RuntimeError("Missing exported executable or PCK")
        # Let the template load its adjacent PCK with no editor project available.
        readback = subprocess.run(
            [str(exe), "--headless", "--", "--check-package"],
            cwd=run, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=35,
        )
        (run / "readback.log").write_bytes(readback.stdout)
        if readback.returncode or b"SANDBOX_RELEASE_LAYOUT_STRIPPED=true" not in readback.stdout:
            raise RuntimeError(readback.stdout.decode(errors="replace"))
        report["stripped_mission_and_sequence"] = True
        report["source_layout_preserved"] = all(sha256(project / name) == digest for name, digest in layout_before.items())
        if not report["source_layout_preserved"]:
            raise RuntimeError("Authored layout changed during export")
        package_before = sha256(package)
        seq = project / "sequence.tres"
        seq.write_text(seq.read_text(encoding="utf-8").replace("fixture.release", "fixture.unregistered"), encoding="utf-8")
        invoke("reimport", editor, "--editor", "--import")
        failure = invoke("export-broken", editor, "--export-release", "Sandbox Windows", exe, expected=1)
        if "fixture.unregistered" not in failure or sha256(package) != package_before:
            raise RuntimeError(f"Strict export failed without the required reference diagnostic or changed the package\n{failure[-3000:]}")
        report["broken_reference_rejected"] = "fixture.unregistered"
        report["existing_package_preserved"] = True
    (run / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
