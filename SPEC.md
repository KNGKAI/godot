# Technical Specification: Modular 3D Sandbox Engine Framework (Godot 4.7.2)

| Attribute | Value |
| :--- | :--- |
| **Document Status** | Draft v1.0 (Engineering Specification) |
| **Target Engine Baseline** | Godot Engine **4.7.2** (C++ Source Fork + Static Modules + GDScript/C# Host API) |
| **Rendering Targets** | Forward+ / Mobile (Direct3D 12 default on Windows, Vulkan, Metal) |
| **Physics Baseline** | Jolt Physics (3D Rigid-Body) decoupled from custom deterministic World Simulation |
| **Primary Scope** | Reusable engine-level infrastructure for 3D sandboxes with live simulations, dynamic events, authored missions/cutscenes, and dual Editor/In-Game Terminal management |

---

## 1. Purpose & Scope Boundaries

This specification defines a reusable, domain-agnostic 3D sandbox framework built on top of Godot Engine 4.7.2. The framework provides high-performance simulation scheduling, decoupled state and event routing, authored narrative/cutscene execution, and synchronized control via both the Godot Editor and diegetic 3D in-game terminals.

To ensure the framework remains reusable across different genres (immersive sims, colony builders, vehicle sandboxes, robotics/industrial simulators), strict ownership boundaries apply:

### Ownership Matrix

| Domain | Framework Ownership (`main/`, `scene/`, `modules/sandbox_*/`) | Host Project Ownership (`res://game/`) |
| :--- | :--- | :--- |
| **State & Routing** | `WorldState`, `EventBus`, `CommandBus`, binary/JSON save serialization | Domain key namespaces, event payload schemas |
| **Simulation** | `SimulationServer` (RIDs, `WorkerThreadPool` scheduling, clocks, snapshots) | Concrete `SimModule` logic (e.g., Economy, Weather, Power Grid, Ecology) |
| **Characters & World** | `SandboxActor` adapter node, `ActorRegistry` | Character controllers (FPS, 3rd-person, RTS, Vehicle), AI, 3D assets, Jolt rigid bodies |
| **Narrative & Cutscenes** | Custom `Animation::TrackType` hooks, `MissionRunner`, `SequenceDirector`, base `Condition`/`Action` | Concrete cutscene actions, dialogue integration, `.tres` missions and sequences |
| **Terminals & Editor** | `TerminalDisplay3D`, `TerminalParser`, permission model, `EditorDock`, `EditorDebuggerPlugin` | Diegetic 3D meshes, CRT/glass shaders, lore-specific commands and roles |

**Explicit Non-Goals:** The framework will never bundle a default character controller, inventory system, combat system, dialogue tree editor, or genre-specific simulation rules.

---

## 2. Engine Baseline & Build Architecture

The framework targets **Godot 4.7.2** (current fully supported stable maintenance release; avoids Godot 4.8 beta's breaking .NET 10 dependency for C# host projects). C++ customizations are partitioned across four tiers to minimize merge conflicts during upstream engine upgrades.

### 2.1 Four-Tier Architecture

1. **Tier 1 — Minimal Core Patches (`main/`, `scene/`):** Strictly isolated patches required only where Godot's `ClassDB` or core main loop cannot be extended externally (multi-clock loop, custom `Animation` tracks, native `Viewport` terminal focus).
2. **Tier 2 — Static C++ Engine Modules (`modules/sandbox_*/`):** Self-contained C++ modules compiled via `SCsub` and `register_types.{h,cpp}`. Chosen over GDExtension for core subsystems because they require unexposed C++ headers (`RID_Owner`, internal containers, `AnimationTrackEditor`) and avoid GDExtension's `ClassDB` ceiling, ABI coupling, and singleton lifecycle limitations.
3. **Tier 3 — Dual Control Interfaces (`modules/sandbox_editor/`, `modules/sandbox_terminal/`):** Native Editor docking panels (`EditorDock`) and diegetic 3D terminal controllers sharing one unified `CommandBus` protocol.
4. **Tier 4 — Host Project Layer (`res://game/`):** Typed GDScript (default for orchestration and UI glue; 34–59% faster than untyped GDScript) and optional C# or nodeless GDExtensions for project-specific compute hot paths. Projects targeting Web exports should avoid C# due to Godot 4 C# web export limitations.

### 2.2 Repository Layout

```text
godot-sandbox-fork/
├── main/
│   └── main.cpp                      # Tier 1: SandboxClock multi-clock iteration hook
├── scene/
│   ├── resources/animation.h         # Tier 1: Custom TrackType enums
│   └── main/viewport.cpp             # Tier 1: Native Terminal Focus routing
└── modules/
    ├── sandbox_core/                 # Tier 2: WorldState, EventBus, CommandBus, Serialization
    ├── sandbox_sim/                  # Tier 2: SimulationServer (RID + WorkerThreadPool), SimModule
    ├── sandbox_narrative/            # Tier 2: MissionRunner, SequenceDirector, SandboxActor
    ├── sandbox_terminal/             # Tier 3: TerminalDisplay3D, TerminalParser, Permissions
    └── sandbox_editor/               # Tier 3: EditorDock, EditorDebuggerPlugin, AnimationTrack UI
```

### 2.3 SCons Build Targets & Compilation Guards

All developer instrumentation, live network bridges, and debug-tier terminal commands must obey Godot's canonical SCons build definitions:

| SCons Target | Preprocessor Defines | Framework Behavior |
| :--- | :--- | :--- |
| `target=editor` | `TOOLS_ENABLED`, `DEBUG_ENABLED` | Full `EditorDock` suite, `EditorDebuggerPlugin`, custom `AnimationTrackEditor` UI, `DEV` terminal permissions, local debug `CommandBus` listener. |
| `target=template_debug` | `DEBUG_ENABLED` | Runtime `EngineDebugger` bridge, `DEV` terminal permissions, opt-in hardened local `CommandBus` listener. |
| `target=template_release` | None | **Production hardened.** All remote debug listeners, `DEV` permission tiers, and editor hooks are compiled out completely via `#ifdef DEBUG_ENABLED`. |

---

## 3. Core Subsystem Specifications

### 3.1 Multi-Clock Main Loop & Physics Determinism Boundary

Stock Godot's `Engine.time_scale` modifies `delta` globally without changing the real-time physics tick rate (causing collision precision loss when `time_scale > 1.0`) and leaves audio unaffected. Furthermore, Godot's default **Jolt Physics** integration does **not** guarantee bit-exact or cross-platform determinism.

#### Specification
* **Tier 1 Patch (`main/main.cpp`):** Introduce three decoupled clocks managed by a C++ `SandboxClock` singleton:
  1. **Simulation Clock (`sim_clock`):** Drives `SimulationServer` at configurable fixed steps. Supports pause, single-step (`step(n)`), and high-speed fast-forward (up to 100x simulated time) without altering 3D rigid-body physics or camera responsiveness.
  2. **Physics & World Clock:** Standard Godot `MainTimerSync` driving Jolt 3D physics and `_physics_process()`. Projects requiring smooth high-refresh rendering must enable **Physics Interpolation** and set `Engine.physics_jitter_fix = 0` when synchronizing with networked or deterministic systems.
  3. **Narrative & UI Clock:** Runs at unscaled real time (or independent cutscene scale) so diegetic 3D terminals, HUDs, and cinematics remain responsive at locked display refresh rates even when `sim_clock` is paused or fast-forwarding.
* **Determinism Boundary:** Deterministic state lives exclusively inside `SimulationServer` (pure math/data simulations). Jolt 3D physics is treated strictly as a visual/interactive physical representation layer, never as the authoritative source of deterministic simulation truth.

---

### 3.2 `SimulationServer`, `WorldState`, & Concurrency (`modules/sandbox_core`, `sandbox_sim`)

Because Godot's active `SceneTree` is strictly single-threaded—and concurrent off-tree scene instantiation across multiple threads is unsafe due to shared global `Resource` loading—all high-frequency simulation logic runs outside the `SceneTree` using Godot's native **Server Pattern**.

#### `SimulationServer` Architecture
* **Handle Management:** Allocates simulation entities and modules via opaque `RID` handles managed by Godot's internal `RID_Owner` configured with `THREAD_SAFE` locking.
* **Memory Layout:** Simulation data is stored in contiguous C++ Structure-of-Arrays (SoA) blocks inside `WorldState` rather than boxed `Variant` dictionaries. Typed GDScript/C# getters and setters expose read/write access across `ClassDB`.
* **Execution Model:** `SimulationServer::step(double sim_delta)` dispatches independent `SimModule` workloads across Godot's `WorkerThreadPool` (`add_task()` / `add_group_task()`).

#### Mandatory Concurrency Invariants
Every C++ module and host-project `SimModule` executing on `WorkerThreadPool` must enforce four non-negotiable rules:
1. **Strict Task Completion:** Every task ID returned by `add_task()` or `add_group_task()` must be awaited via `wait_for_task_completion()` or `wait_for_group_task_completion()` before the simulation step concludes to prevent internal allocation leaks.
2. **Zero Nested Waits:** Worker tasks must never call `wait_for_*` on other worker tasks (prevents pool deadlock and `@GlobalScope.ERR_BUSY` aborts).
3. **Immutable Container Sizing:** No shared array or SoA buffer may be resized, appended, or shrunk during parallel worker execution; structural mutations are batched into a single-threaded commit phase at the end of the sim tick.
4. **Deferred SceneTree Crossing:** Worker threads must never mutate active `SceneTree` nodes directly. State deltas and threshold events are queued in a mutex-guarded ring buffer and flushed on the main thread via `EventBus` signals or `call_deferred()` / `call_thread_safe()`.

#### Host Project `SimModule` Interface
Host projects extend an abstract `SimModule` resource (in GDScript, C#, or C++) to plug custom simulations into the runner:

```gdscript
# Exposed contract for host-project simulations
@abstract
class_name SimModule
extends Resource

@export var module_id: StringName
@export var tick_rate_hz: float = 2.0
@export var priority: int = 0
@export var thread_safe_tick: bool = false # Opt-in for WorkerThreadPool execution

func on_init(state: WorldState, events: EventBus) -> void: pass
func on_tick(delta_sim: float, state: WorldState, events: EventBus) -> void: pass
func on_snapshot_write() -> PackedByteArray: return PackedByteArray()
func on_snapshot_read(data: PackedByteArray) -> void: pass
```

---

### 3.3 `EventBus` & `CommandBus` (`modules/sandbox_core`)

#### `EventBus` (C++ Singleton)
* Typed, decoupled publish-subscribe router bridging `SimulationServer`, `MissionRunner`, 3D world props, and terminals.
* Supports main-thread deferred dispatch (`dispatch_deferred(event_id, payload)`) for events originating from background simulation threads, plus structured ring-buffer logging for Editor/Terminal inspection.

#### `CommandBus` (C++ Singleton)
* Single mutation entry point shared identically by in-game 3D terminals, the Godot Editor dock, automated test harnesses, and debug remote consoles.
* **Command Schema:** Every registered command defines:
  * `name: StringName` (namespaced, e.g., `sim.pause`, `state.set`, `event.fire`, `mission.start`)
  * `min_permission: PermissionLevel` (`GUEST`, `USER`, `ADMIN`, `DEV`)
  * `arg_signature: Array[Variant.Type]` (strictly validated prior to execution)
  * `handler: Callable`
  * `help_text: String` (powers terminal autocomplete and `help` output)
* **Audit Logging:** Every execution returns a structured `CommandResult` (`ok: bool`, `output: String`, `error_code: Error`) and logs `(timestamp, caller_id, permission_level, command_string)`.

---

### 3.4 Domain-Agnostic Actor Contract (`SandboxActor` & `ActorRegistry`)

To keep cutscenes, missions, and terminals completely decoupled from host-project character controllers, any controllable entity (FPS player, 3rd-person rig, vehicle, RTS camera, NPC) attaches a `SandboxActor` child node.

```gdscript
# C++ Node exposed by modules/sandbox_narrative/
class_name SandboxActor
extends Node

@export var actor_id: StringName      # Unique ID, e.g., &"player", &"drone_04"
@export var tags: Array[StringName]   # Group targeting, e.g., [&"civilian", &"sector_a"]

# Signals consumed by the host project's controller
signal control_lock_requested(locked: bool, reason: StringName)
signal teleport_requested(target_transform: Transform3D)
signal action_requested(action_id: StringName, payload: Dictionary)
```

* **Physics Interpolation Requirement:** Host controllers responding to `teleport_requested` must invoke `reset_physics_interpolation()` on their root `Node3D` immediately after updating `global_transform` to prevent visual streaking between physics ticks.
* **Lifecycle:** `SandboxActor` auto-registers with `ActorRegistry` on `_enter_tree()` and unregisters on `_exit_tree()`.

---

### 3.5 Authored Sequences, Cutscenes, & Missions (`modules/sandbox_narrative`)

Because Godot's `AnimationTrackEditor` (~10,000 lines of C++) is unexposed to `ClassDB`, extending `AnimationPlayer` with custom tracks requires engine-level C++ changes. The framework combines **native timeline tracks** for linear, frame-accurate cinematics with a **data-driven coroutine director** for dynamic, branching sequences.

#### Layer A: Native `AnimationPlayer` Tracks (Tier 1 Core Patch)
Patch `scene/resources/animation.h`, `animation_mixer.cpp`, and `editor/animation_track_editor.cpp` to add three native `Animation::TrackType` values:
* `TYPE_EVENT_BUS`: Fires typed `EventBus` signals at exact keyframe timestamps.
* `TYPE_WORLD_STATE`: Sets or interpolates `WorldState` variables directly along the timeline.
* `TYPE_ACTOR_COMMAND`: Dispatches `action_requested` or `control_lock_requested` to an `actor_id` via `ActorRegistry`.

This grants designers full timeline scrubbing, audio sync, Bézier curves, and collapsible node track summaries (Godot 4.7) with zero custom timeline UI code.

#### Layer B: `SequenceDirector` & `MissionRunner` (Tier 2 C++ Module)
For branching cinematics, dynamic mission objectives, and runtime conditions where linear timelines are too rigid:
* **`MissionData` (Resource):** Holds `mission_id`, `objectives: Array[ObjectiveData]`, `on_start_sequence`, and `on_complete_sequence`.
* **`SandboxCondition` (Abstract Resource):** `evaluate(state, event_id, payload) -> bool`. Framework ships generics (`CondStateEquals`, `CondStateRange`, `CondEventFired`, `CondAll`, `CondAny`); host projects subclass for domain checks.
* **`SequenceAction` (Abstract Resource):** Asynchronous execution unit supporting sequential and parallel steps, leveraging Godot 4.7's `Tween.tween_await()` for clean non-blocking waits. Framework ships `ActionPlayAnimation`, `ActionSetState`, `ActionEmitEvent`, `ActionRunCommand`, `ActionLockActor`, and `ActionWaitEvent`.
* **Camera Interop:** Camera blending remains decoupled from the core module; host projects may bind `ActionBlendCamera` to standard `Camera3D` tweens or community addons such as **Phantom Camera**.

---

### 3.6 Diegetic 3D Terminals (`modules/sandbox_terminal`)

Interactive in-world 3D monitors (`TerminalDisplay3D`) wrap a 2D `Control` CLI inside a `SubViewport` mapped to a 3D surface. The design explicitly mitigates Godot 4's documented `SubViewport` rendering, focus, and input traps.

#### Rendering & Performance Rules
1. **2D-Only Isolation:** Every terminal `SubViewport` must enforce `disable_3d = true` and omit any `WorldEnvironment` child to prevent redundant 3D render passes and linear CPU occlusion-culling multiplication.
2. **Mipmap Deficit Mitigation:** Because Godot's `ViewportTexture` does not natively generate mipmaps (causing severe distance aliasing), `TerminalDisplay3D` mandates:
   * **MSDF (Multichannel Signed Distance Field)** font rendering on all terminal text labels.
   * Configurable `Viewport.oversampling` / `oversampling_override` and integer-aligned `SubViewport` resolutions (`scaling_3d_scale = 1.0`).
   * Optional distance-based `SubViewport` throttling (`UPDATE_DISABLED` when outside player interaction range or frustum).
3. **No `SubViewportContainer` Wrapper:** `TerminalDisplay3D` must never place its `SubViewport` inside a `SubViewportContainer`, as visibility toggles on `SubViewportContainer` silently overwrite `UPDATE_ONCE` back to `UPDATE_ALWAYS`.
4. **Color Space Awareness:** Terminal surface shaders sampling `ViewportTexture` (sRGB-encoded LDR when `use_hdr_2d = false`) must explicitly declare `source_color` / sRGB-to-linear conversion so CRT scanlines, bloom, and AgX tonemapping behave consistently.
5. **FSR2 Guardrail:** Terminal viewports must maintain a minimum resolution of at least 256x256 to avoid known FSR2 compute-job errors on tiny viewports.

#### Input Forwarding & Terminal Focus Mode
* **3D-to-2D Coordinate Synthesis:** An `Area3D` collider aligned to the terminal screen intercepts raycasts, transforms the hit point into local quad space (`quad.global_transform.affine_inverse()`), normalizes UV coordinates to `SubViewport.size`, synthesizes `position`, `relative`, and `velocity`, and dispatches via `SubViewport.push_input()`.
* **Captured Mouse Handling:** Because `Input.MOUSE_MODE_CAPTURED` (standard in FPS games) locks `InputEventMouseMotion.position` to screen center and zeroes `screen_velocity`, `TerminalDisplay3D` uses explicit camera-center raycasting rather than cursor physics picking while in captured mode.
* **Tier 1 Patch — Native Terminal Focus Mode:** When a player engages a terminal, `TerminalDisplay3D` locks the player's `SandboxActor` (`control_lock_requested(true, &"terminal")`) and activates a native `Viewport` focus override that routes keyboard and scroll events directly into the target `SubViewport` until an escape key (`ui_cancel`) releases focus.

#### Permission Model
Each `TerminalDisplay3D` exports a `permission_tier` (`GUEST`, `USER`, `ADMIN`, `DEV`) and an optional `allowed_namespaces: Array[StringName]` whitelist (e.g., `[&"power", &"doors"]`). Diegetic in-world terminals can only execute commands matching both their tier and namespace whitelist; `DEV` tier is compiled out in `template_release`.

---

### 3.7 Godot Editor Integration (`modules/sandbox_editor`)

* **Modern Dock Architecture:** Uses Godot 4.6+'s movable, floatable **`EditorDock`** API (`EditorPlugin.add_dock()`), avoiding deprecated legacy bottom-panel APIs. Provides:
  * **Asset Browser Tab:** Scans `res://` for `SimModule`, `MissionData`, and `SequenceData` `.tres` assets for rapid inspection and creation.
  * **Live WorldState Inspector:** Displays real-time SoA variables, active missions, and `SimModule` tick timings (microseconds per tick).
  * **Command Console:** Executes `CommandBus` commands directly against the running game instance.
* **Bidirectional Runtime IPC:** Uses `EditorDebuggerPlugin` (Editor side) paired with `EngineDebugger` (Game side):
  * *Editor to Game:* Commands are sent via `EditorDebuggerSession.send_message("sandbox:cmd", [raw_cmd])` and received via `EngineDebugger.register_message_capture("sandbox", ...)`.
  * *Game to Editor:* State snapshots, profiler metrics (`profiler_add_frame_data()`), and event logs stream back via `EngineDebugger.send_message("sandbox:telemetry", [...])`.

---

## 4. Serialization, Save/Load, & Time-Scrubbing

Godot's native `.tres`/`.res` formats (`ResourceSaver`) can embed executable scripts, and binary deserializers with `allow_objects = true` permit arbitrary code execution. **Using `ResourceSaver` or object deserialization for player save files is strictly prohibited.**

### 4.1 Hardened Save Format Specification
* **Allowed Formats:**
  * **Binary Snapshot (`.sbsav`):** Used for rapid autosaves, network sync, and in-memory rewind ring buffers. Encoded via custom C++ `StreamPeerBuffer` writing strictly primitive types, fixed headers, and `PackedByteArray` blobs from `SimulationServer` SoA memory.
  * **Structured JSON (`.json`):** Used for human-readable exports, modding, or bug reports.
* **Deserializer Guardrail:** All calls to `bytes_to_var()`, `FileAccess.get_var()`, `PacketPeer.get_var()`, `StreamPeer.get_var()`, and `Marshalls.base64_to_variant()` across the entire codebase must strictly enforce `allow_objects = false`.
* **Schema Envelope:**

```json
{
  "schema_version": 1,
  "engine_version": "4.7.2-sandbox",
  "sim_tick_index": 14820,
  "sim_time_elapsed": 7410.0,
  "world_state": {
    "weather.rain_intensity": 0.42,
    "power.sector_a_load": 88.5
  },
  "sim_modules": {
    "economy": "<base64_primitive_blob>"
  },
  "missions": {
    "active": ["heist_01"],
    "completed": ["intro_00"],
    "objective_progress": {}
  }
}
```

### 4.2 Deterministic Simulation Rewind (Optional Debug/Gameplay Feature)
Because `SimulationServer` stores state in contiguous C++ SoA arrays, the server maintains an optional ring buffer of compressed binary snapshots taken every $N$ simulation ticks. Commands such as `sim.rewind <ticks>` restore `WorldState` and emit a `world_state_restored` event on `EventBus`, allowing 3D actors and props to resynchronize visually.

---

## 5. Security & Hardening Specification

| Threat Vector | Affected Component | Mandatory Mitigation |
| :--- | :--- | :--- |
| **Deserialization RCE** | Save/Load, Modding, Network Sync | Ban `ResourceSaver`/`ResourceLoader` for untrusted saves; enforce `allow_objects = false` on all Godot Variant deserializers; validate schema versions and numeric bounds before applying state. |
| **Cross-Site WebSocket Hijacking (CSWSH)** | Local Debug `CommandBus` Server (TCP/WS) | Strictly validate HTTP `Origin` header against an explicit allowlist (reject browser origins); require a cryptographically random per-session Bearer token/nonce generated at engine startup and passed out-of-band. |
| **DNS Rebinding & `0.0.0.0` Bypass** | Local Debug `CommandBus` Server | Bind strictly to `127.0.0.1` / `::1`; validate `Host` header strictly against `localhost` or `127.0.0.1`; explicitly reject `0.0.0.0/8` requests. |
| **Debug Surface Leakage in Shipping Builds** | `CommandBus` Server, `DEV` Terminals | Wrap all remote debug server code and `DEV` command registrations inside `#ifdef DEBUG_ENABLED`. Verify zero symbols/listeners exist in `target=template_release` binaries via automated CI checks. |
| **Command Injection / DoS** | Diegetic 3D Terminals | Enforce strict `PermissionLevel` and namespace whitelists per terminal instance; cap input buffer length (e.g., 512 bytes) and rate-limit command execution per second. |

---

## 6. Verification & Acceptance Criteria

Before a framework milestone is marked production-ready, it must pass the following automated and manual verification gates:

1. **Multi-Clock Isolation Test:** Run `sim_clock` at `50x` speed and paused (`0x`) while verifying that Jolt 3D physics maintains 60 Hz stability, audio playback speed remains `1.0`, and 3D terminal input latency stays below 16.6 ms.
2. **Thread Safety & Leak Audit:** Execute 10,000 consecutive `SimulationServer` ticks across 8 concurrent `SimModule`s on `WorkerThreadPool` under AddressSanitizer / ThreadSanitizer with zero data races, zero `ERR_BUSY` aborts, and zero leaked task handles.
3. **Domain Agnosticism Proof:** Mount the compiled engine onto two distinct test harness projects—(a) a 1st-person `CharacterBody3D` scene and (b) an RTS top-down camera scene—executing the exact same `MissionData`, `AnimationPlayer` cutscene, and `TerminalDisplay3D` interaction without modifying engine code.
4. **3D Terminal Legibility & Performance Gate:** Render 4 simultaneous `TerminalDisplay3D` monitors in a 3D room; verify readable MSDF text at 1 m–8 m distances and 45-degree viewing angles, confirm `disable_3d = true` eliminates duplicate occlusion culling passes, and verify captured-mouse interaction plus `ui_cancel` focus release.
5. **Security Verification:**
   * Attempt to load a crafted save file containing an embedded GDScript `Object` payload; confirm safe rejection with `ERR_INVALID_DATA`.
   * Attempt an unauthenticated or mismatched-`Origin` WebSocket handshake against the debug `CommandBus` port; confirm termination with HTTP `403 Forbidden`.
   * Audit `target=template_release` export; confirm no listening debug sockets open and `DEV` commands return `ERR_UNAUTHORIZED` / `Unknown command`.