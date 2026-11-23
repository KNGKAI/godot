# Repository Guidelines

## Project Structure

This repository contains the Godot engine and editor. Put shared types in `core/`, nodes and resources in `scene/`, and low-level rendering, physics, and audio services in `servers/`. Editor tools live in `editor/`, with their SVG icons in `editor/icons/`. Use `modules/` for optional engine features, `platform/` for OS integration, and `drivers/` for backend implementations. Class reference XML lives in `doc/classes/`.

For work on the modular 3D sandbox framework, always consult and maintain the three root tracking documents:

- **[SPEC.md](SPEC.md) (Specification):** Authoritative Draft v1.2 engineering specification covering the four-tier architecture (`main/`, `scene/`, `editor/`, and `modules/sandbox_{core,sim,streaming,bubble,narrative,terminal,editor}/`), ownership boundaries, security mitigations, and the 8 acceptance gates.
- **[SCOPE.md](SCOPE.md) (Implementation Scope):** Codebase audit findings (Findings 1–10), Godot 4.8-beta vs. 4.7.2 API constraints, concurrency invariants, and the 8-phase work breakdown structure.
- **[STATUS.md](STATUS.md) (Implementation Status):** Live phase-by-phase checklist, file completion tracking, verification gate results (`Gate 1`–`Gate 8`), and milestone log.

## Sandbox Development Workflow

1. **Pre-Flight Check:** Read `SPEC.md`, `SCOPE.md`, and `STATUS.md` before starting or resuming any implementation phase.
2. **Architectural & Security Invariants:**
   - Keep Tier 1 patches (`main/main.cpp`, `scene/resources/animation.{h,cpp}`, `scene/animation/animation_mixer.{h,cpp}`, `editor/animation/animation_track_editor.cpp`, `scene/main/viewport.{h,cpp}`) minimal and self-contained.
   - Enforce all four `WorkerThreadPool` concurrency rules (await every task handle, zero nested waits, immutable SoA container sizing during parallel ticks, deferred main-thread `SceneTree` crossing).
   - Enforce `allow_objects = false` on all deserializers, normalize and restrict `WorldPartitionManifest` paths strictly to `res://`, and guard all debug listeners and `DEV` commands behind `#ifdef DEBUG_ENABLED`.
3. **Incremental Verification:** Build the editor with unit tests enabled (`scons platform=windows target=editor arch=x86_64 tests=yes`) and run the headless doctest suite (`.\bin\godot.windows.editor.x86_64.exe --headless --test`) at the end of each phase.
4. **Status Tracking:** Update `STATUS.md` immediately whenever a file, subsystem phase, or verification gate (`Gate 1`–`Gate 8`) is completed.

## Build and Run

Run these PowerShell commands from the repository root with SCons and the x64 MSVC toolchain available.

- `scons platform=windows target=editor arch=x86_64 tests=yes` builds the editor with unit tests enabled. Rerun it after editing C++.
- `.\bin\godot.windows.editor.x86_64.exe --project-manager` launches the project manager.
- `.\bin\godot.windows.editor.x86_64.exe --editor --path C:\path\to\project` opens a directory containing `project.godot`.

## Coding Style

Use tabs displayed at four columns for C++, four spaces for Python and SCons files, and two spaces for YAML. Follow `.editorconfig` for line endings and whitespace. Use `PascalCase` for C++ classes, `snake_case` for methods and filenames, and the `p_` prefix for parameters. Run `prek run` on staged changes to apply configured checks, including clang-format and Ruff.

## Testing

Run `.\bin\godot.windows.editor.x86_64.exe --headless --test` for the engine unit suite. Place doctest cases in the matching subsystem under `tests/`. For example, `tests/core/math/test_aabb.cpp` uses names such as `TEST_CASE("[AABB] Volume getters")`. Cover the changed behavior and include the triggering input when fixing a regression. Store shared test fixtures in `tests/data/`.

## Commits and Pull Requests

Write an imperative English subject and aim for fewer than 72 characters. Recent commits use subjects such as “Fix subfolder validation problems in the plugin dialog” and optional subsystem prefixes such as `OpenXR:`.

Target `master` and keep each PR focused on one change. Use the PR template to describe the problem, link the issue with `Closes #123`, and explain how you tested the change. Disclose any AI assistance and how you used it. See [CONTRIBUTING.md](CONTRIBUTING.md) for the review workflow.
