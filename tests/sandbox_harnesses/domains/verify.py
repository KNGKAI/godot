"""Run two independent Gate 3 projects with identical sandbox resources."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[2]


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def export_host(editor, template, project, work, env):
    version = subprocess.check_output([str(editor), "--version"], env=env, text=True, timeout=25).strip()
    editor_config = Path(env["APPDATA"]) / "Godot"
    editor_config.mkdir(parents=True)
    version_pair = ".".join(version.split(".")[:2])
    (editor_config / f"editor_settings-{version_pair}.tres").write_text(
        '[gd_resource type="EditorSettings" format=3]\n\n[resource]\nnetwork/tls/editor_tls_certificates='
        + json.dumps((project / "ca-bundle.crt").as_posix()) + "\n", encoding="utf-8"
    )
    preset = (
        '[preset.0]\nname="Gate 3 Windows"\nplatform="Windows Desktop"\nrunnable=true\n'
        'export_filter="all_resources"\ninclude_filter="*.crt"\nexclude_filter=""\n'
        '[preset.0.options]\ncustom_template/release=' + json.dumps(template.as_posix()) +
        '\napplication/modify_resources=false\nbinary_format/embed_pck=false\ndebug/export_console_wrapper=0\n'
    )
    (project / "export_presets.cfg").write_text(preset, encoding="utf-8")
    output = work / "export"
    output.mkdir()
    exe = output / "arrival.exe"
    for label, options in (("import", ["--editor", "--import"]), ("export", ["--export-release", "Gate 3 Windows", str(exe)])):
        run = subprocess.run(
            [str(editor), "--headless", "--path", str(project), *options],
            cwd=project, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
        )
        (work / (label + ".log")).write_bytes(run.stdout)
        if run.returncode or b"ERROR:" in run.stdout:
            raise RuntimeError(f"{label} failed: {run.stdout.decode(errors='replace')}")
    pck = exe.with_suffix(".pck")
    return exe, {"exe_sha256": sha256(exe), "pck_sha256": sha256(pck), "pck_bytes": pck.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "bin/godot.windows.editor.x86_64.exe")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--template", type=Path, help="Export and run each host on this Windows release template")
    args = parser.parse_args()
    engine = args.engine.resolve()
    report = {"engine_sha256": sha256(engine), "shared_sha256": {}, "hosts": []}
    template = args.template.resolve(strict=True) if args.template else None
    if template:
        report["release_template_sha256"] = sha256(template)
    run_dir = ROOT / "bin" / "phase8" / ("domains-" + uuid.uuid4().hex[:10])
    for host in ("first_person", "rts"):
        work = run_dir / host
        work.mkdir(parents=True)
        project = work / "project"
        shutil.copytree(SOURCE / host, project)
        # Isolated Windows profiles cannot read the system certificate store.
        bundle = ROOT / "thirdparty/certs/ca-bundle.crt"
        shutil.copyfile(bundle, project / "ca-bundle.crt")
        with (project / "project.godot").open("a", encoding="utf-8") as settings:
            settings.write('\n[network]\ntls/certificate_bundle_override="res://ca-bundle.crt"\n')
        staged_config_sha256 = sha256(project / "project.godot")
        shared = project / "shared"
        shutil.copytree(SOURCE / "shared", shared)
        hashes = {f.name: sha256(f) for f in sorted(shared.iterdir()) if f.is_file()}
        if report["shared_sha256"] and hashes != report["shared_sha256"]:
            raise RuntimeError("Shared resources differ between projects")
        report["shared_sha256"] = hashes
        profile = work / "profile"
        profile.mkdir()
        env = os.environ.copy()
        for name in ("APPDATA", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_CONFIG_HOME"):
            env[name] = str(profile)
        executable = engine
        launch_dir = project
        options = ["--path", str(project)]
        package = None
        if template:
            executable, package = export_host(engine, template, project, work, env)
            if package["exe_sha256"] != report["release_template_sha256"]:
                raise RuntimeError("Exported executable differs from the specified template")
            if any(sha256(shared / name) != digest for name, digest in hashes.items()):
                raise RuntimeError("Export changed an authored shared resource")
            launch_dir = executable.parent
            options = []
        run = subprocess.run(
            [str(executable), "--headless", "--fixed-fps", "60", *options, "--log-file", str(work / "game.log")],
            cwd=launch_dir,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
            errors="replace",
            timeout=40,
        )
        lines = [line for line in run.stdout.splitlines() if line.startswith("GATE3_RESULT ")]
        if len(lines) != 1:
            raise RuntimeError(f"{host} did not finish\n{run.stdout}")
        observation = json.loads(lines[0].removeprefix("GATE3_RESULT "))
        observation["staged_project_sha256"] = staged_config_sha256
        if package:
            observation["export"] = package
        diagnostics = [line for line in run.stdout.splitlines() if "ERROR:" in line]
        observation["engine_diagnostics"] = diagnostics
        observation["engine_warnings"] = [line for line in run.stdout.splitlines() if "WARNING:" in line]
        if diagnostics:
            observation["failures"].append("Engine error during the host run")
        observation["exit_code"] = run.returncode
        report["hosts"].append(observation)
        if observation["host"] != host:
            raise RuntimeError(f"Unexpected host {observation['host']}")
    result = json.dumps(report, indent=2) + "\n"
    print(result, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding="utf-8")
    return int(any(host["exit_code"] or host["failures"] for host in report["hosts"]))


if __name__ == "__main__":
    raise SystemExit(main())
