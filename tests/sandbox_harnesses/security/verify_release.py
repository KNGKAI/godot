"""Audit a Windows template_release executable's sandbox listener and DEV command handling."""

import argparse
import errno
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path


def read_report(path: Path, process: subprocess.Popen) -> dict:
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        if path.is_file():
            try:
                return json.loads(path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                pass  # The engine may still be writing the report.
        if process.poll() is not None:
            raise RuntimeError(f"Engine exited with code {process.returncode} before writing {path.name}")
        time.sleep(0.05)
    raise TimeoutError(f"Engine did not write {path.name}")


def tcp_rows(pid: int) -> list[list[str]]:
    # Without a protocol filter, netstat reports both IPv4 and IPv6.
    output = subprocess.check_output(["netstat", "-ano"], text=True, errors="replace")
    rows = (line.split() for line in output.splitlines())
    return [row for row in rows if len(row) == 5 and row[0] == "TCP" and row[-1] == str(pid)]


def verify(engine: Path, directory: Path) -> bool:
    directory.mkdir(parents=True, exist_ok=False)
    project = directory / "project"
    project.mkdir()
    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="Sandbox Release Probe"\n'
        'run/main_scene="res://main.tscn"\n'
        '[physics]\n2d/physics_engine="Dummy"\n'
        '[rendering]\nrenderer/rendering_method="gl_compatibility"\n',
        encoding="utf-8",
    )
    project_data = project / ".godot"
    project_data.mkdir()
    (project_data / "global_script_class_cache.cfg").write_text("list=[]\n", encoding="utf-8")
    (project / "main.tscn").write_text(
        '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://release_probe.gd" id="1"]\n'
        '[node name="ReleaseProbe" type="Node"]\nscript = ExtResource("1")\n',
        encoding="utf-8",
    )
    shutil.copyfile(Path(__file__).with_name("release_probe.gd"), project / "release_probe.gd")
    # Release templates start alongside the executable when path overrides are disabled.
    executable = project / engine.name
    shutil.copyfile(engine, executable)
    env = os.environ.copy()
    for variable in ("APPDATA", "LOCALAPPDATA"):
        cache = directory / variable
        cache.mkdir()
        env[variable] = str(cache)
    with socket.socket() as picker:
        picker.bind(("127.0.0.1", 0))
        requested_port = picker.getsockname()[1]

    command = [str(executable), "--headless", "--log-file", str(directory / "godot.log"), "--", str(requested_port)]
    with (directory / "runtime.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            result = read_report(project / "listening.json", process)
            rows = tcp_rows(process.pid)
            control_port = result["control_port"]
            control = [row for row in rows if row[1] == f"127.0.0.1:{control_port}" and row[2] == "0.0.0.0:0"]
            if result["pid"] != process.pid or not result["control_started"] or len(control) != 1:
                raise RuntimeError(f"Could not observe the positive-control listener for PID {process.pid}: {rows}")
            listener_state = control[0][3]  # Windows localizes the netstat column values.
            with socket.create_connection(("127.0.0.1", control_port), timeout=2):
                pass
            (project / "close-control").touch()
            read_report(project / "closed.json", process)
            samples = []
            for _ in range(3):
                samples.append([row for row in tcp_rows(process.pid) if row[3] == listener_state])
                time.sleep(0.2)
            with socket.socket() as connection:
                connection.settimeout(8)
                connect_result = connection.connect_ex(("127.0.0.1", requested_port))
            (project / "finish").touch()
            exit_code = process.wait(timeout=15)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)

    evidence = {
        "source_executable": str(engine),
        "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "command": command,
        "engine_report": result,
        "positive_control": control,
        "listener_samples_after_control_closed": samples,
        "requested_port_connect_ex": connect_result,
        "exit_code": exit_code,
    }
    report_path = directory / "evidence.json"
    report_path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    passed = result["passed"] and not any(samples) and connect_result == errno.WSAECONNREFUSED and exit_code == 0
    print(f"{'PASS' if passed else 'FAIL'}: {report_path}")
    return passed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path, help="Windows template_release .exe (direct executable)")
    parser.add_argument("directory", type=Path, help="New directory for the project, logs and evidence.json")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("This probe reads the Windows process socket table")
    sys.exit(0 if verify(args.engine.resolve(), args.directory.resolve()) else 1)
