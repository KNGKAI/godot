# Gate 3 cross-domain mission playback

From the repository root, run the two host projects against the same sandbox-enabled editor.

```powershell
python tests/sandbox_harnesses/domains/verify.py --output tests/sandbox_harnesses/domains/results/gate3.json
```

The runner stages two independent projects under `bin/phase8/domains-*`, copies the same mission, animation, actor, terminal and probe into each project, and compares their SHA-256 digests. Pass `--engine PATH` to select another executable. Each host runs headless at a fixed 60 FPS. Its staged project and Godot log remain beside its isolated profile.

The first-person host mounts its eye camera on a `CharacterBody3D`. The RTS host uses an overhead orthogonal `Camera3D`. Both aim at the same terminal scene, click it through the active camera, type `mission.start arrival`, submit with Enter and release focus with Escape.

The mission's start sequence plays the actor's `AnimationPlayer` and waits for its cue. The three native tracks interpolate signal strength, send a focus action to the host and fire the objective event with a sector payload. The first-person controller narrows the field of view to 50 degrees, and the RTS controller zooms the orthogonal view to 4 m. The probe checks both controllers' responses, objective completion, the waiting branch's acknowledgement and exactly one debrief from the completion sequence. It also exercises mission inspection, repeat completion and an unknown mission through the terminal.

The [recorded result](results/gate3.json) includes the executable and shared-resource digests, per-host observations and engine diagnostics.

## Windows release export

```powershell
python tests/sandbox_harnesses/domains/verify.py --template bin/phase8/release-source/bin/godot.windows.template_release.x86_64.exe --output tests/sandbox_harnesses/domains/results/gate3-release.json
```

This run imports and exports both hosts, then launches each EXE beside its PCK from a separate directory. The runner verifies that each executable matches the shipping template byte for byte and that export preserves all five authored shared files. Each editor and release run stages the same per-host configuration and the repository's certificate bundle. The release template includes Jolt for 3D and reports a dummy backend for unused 2D physics. [Release observations and package hashes](results/gate3-release.json)

The first export used the implicit 3D physics setting. This build registers Jolt by name and omits Godot Physics, which normally provides the legacy default. Both games started with dummy 3D physics, so the terminal rays missed. Both projects now select `Jolt Physics` explicitly. [Original runtime observations](results/gate3-release-default-physics.json)
