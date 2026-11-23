# Technical Specification: Modular 3D Sandbox Engine Framework (Godot 4.7.2)

| Attribute | Value |
| :--- | :--- |
| **Document Status** | Draft v1.2 (Visual Narrative Authoring & Schema-Driven Inspector Expansion) |
| **Target Engine Baseline** | Godot Engine **4.7.2** (C++ Source Fork + Static Modules + GDScript/C# Host API) |
| **Rendering Targets** | Forward+ / Mobile (Direct3D 12 default on Windows, Vulkan, Metal) |
| **Physics Baseline** | Jolt Physics (3D Rigid-Body) decoupled from custom deterministic World Simulation |
| **Primary Scope** | Reusable engine-level infrastructure scaling from linear cinematic levels to systemic open worlds and dense urban sandboxes, featuring live simulations, dynamic events, visual mission/sequence authoring, schema-validated inspector tooling, spatial streaming, actor hydration, and dual Editor/In-Game Terminal management |

## 1. Purpose & Scope Boundaries

This specification defines a reusable, domain-agnostic 3D sandbox framework built on top of Godot Engine 4.7.2. The framework provides high-performance simulation scheduling, decoupled state and event routing, authored narrative/cutscene execution, visual node-graph mission authoring, schema-driven editor pickers, opt-in open-world spatial streaming, SoA-to-Node actor hydration, and synchronized control via both the Godot Editor and diegetic 3D in-game terminals.

To ensure the framework remains reusable across radically different genres—from bounded linear shooters to systemic open worlds, dense urban sandboxes, colony builders, and industrial simulators—strict ownership boundaries apply:

### Ownership Matrix

| Domain | Framework Ownership (`main/`, `scene/`, `modules/sandbox_*/`) | Host Project Ownership (`res://game/`) |
| :--- | :--- | :--- |
| **State & Routing** | `WorldState`, `EventBus`, `CommandBus`, binary/JSON save serialization | Domain key namespaces, event payload schemas |
| **Simulation** | `SimulationServer` (RIDs, `WorkerThreadPool` scheduling, clocks, snapshots) | Concrete `SimModule` logic (e.g., Economy, Weather, Power Grid, Fire, Traffic Math) |
| **World Streaming & Precision** | `StreamingServer`, `WorldPartitionGrid`, per-LOD VRAM unloading, HLOD swapper, `OriginShifter` / LWC bridge | Terrain & city art, cell budgets, HLOD bake profiles, biome/district definitions |
| **Entity Hydration & Crowds** | `HydrationBubble`, `ActorNodePool`, `MultiMesh` proxy feeder, SoA-to-Node lifecycle bridge | Vehicle/NPC prefabs, pool sizes, traffic splines, crowd AI, damage & ragdoll logic |
| **Characters & World** | `SandboxActor` adapter node, `ActorRegistry` | Character controllers (FPS, 3rd-person, RTS, Vehicle), combat AI, 3D assets, Jolt rigid bodies |
| **Narrative & Cutscenes** | Custom `Animation::TrackType` hooks, `MissionRunner`, `SequenceDirector`, base `Condition`/`Action` | Concrete cutscene actions, dialogue integration, `.tres` missions and sequences |
| **Terminals & Editor** | `TerminalDisplay3D`, `TerminalParser`, permission model, `EditorDock`, `GraphEdit` Narrative Editor, `EditorInspectorPlugin` ID pickers, `SandboxSchemaRegistry`, `EditorDebuggerPlugin` | Diegetic 3D meshes, CRT/glass shaders, lore-specific commands/roles, project schema entries, custom condition/action icons |

**Explicit Non-Goals:** The framework will never bundle a default character controller, inventory system, combat/ballistics system, vehicle physics model, dialogue tree editor, or genre-specific simulation rules.

## 2. Engine Baseline & Build Architecture

The framework targets **Godot 4.7.2** (fully supported stable maintenance release; avoids Godot 4.8 beta's breaking .NET 10 dependency for C# host projects). C++ customizations are partitioned across four tiers to minimize merge conflicts during upstream engine upgrades.

### 2.1 Four-Tier Architecture

1. **Tier 1 — Minimal Core Patches (`main/`, `scene/`):** Strictly isolated patches required only where Godot's `ClassDB` or core main loop cannot be extended externally (multi-clock loop, custom `Animation` tracks, native `Viewport` terminal focus).
2. **Tier 2 — Static C++ Engine Modules (`modules/sandbox_*/`):** Self-contained C++ modules compiled via `SCsub` and `register_types.{h,cpp}`. Chosen over GDExtension for core subsystems because they require unexposed C++ headers (`RID_Owner`, internal containers, `AnimationTrackEditor`, direct `RenderingServer` batching) and avoid GDExtension's `ClassDB` ceiling, ABI coupling, and singleton lifecycle limitations. `sandbox_streaming` and `sandbox_bubble` are strictly **opt-in** at runtime so bounded linear games incur zero open-world overhead.
3. **Tier 3 — Dual Control Interfaces (`modules/sandbox_editor/`, `modules/sandbox_terminal/`):** Native Editor docking panels (`EditorDock`), `GraphEdit` mission/sequence canvas, schema-validated `EditorInspectorPlugin` pickers, and diegetic 3D terminal controllers sharing one unified `CommandBus` protocol.
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
    ├── sandbox_core/                 # Tier 2: WorldState, EventBus, CommandBus, Serialization, SchemaRegistry
    ├── sandbox_sim/                  # Tier 2: SimulationServer (RID + WorkerThreadPool), SimModule
    ├── sandbox_streaming/            # Tier 2 (Opt-In): WorldPartitionGrid, Per-LOD Async Loader, HLOD, OriginShifter
    ├── sandbox_bubble/               # Tier 2 (Opt-In): HydrationBubble, ActorNodePool, MultiMesh Proxy Bridge
    ├── sandbox_narrative/            # Tier 2: MissionRunner, SequenceDirector, SandboxActor
    ├── sandbox_terminal/             # Tier 3: TerminalDisplay3D, TerminalParser, Permissions
    └── sandbox_editor/               # Tier 3: EditorDock, GraphEdit Narrative Editor, Inspector ID Pickers,
                                      #         EditorDebuggerPlugin, HLOD Baker & AnimationTrack UI
```

### 2.3 SCons Build Targets & Compilation Guards

All developer instrumentation, live network bridges, and debug-tier terminal commands must obey Godot's canonical SCons build definitions:

| SCons Target | Preprocessor Defines | Framework Behavior |
| :--- | :--- | :--- |
| `target=editor` | `TOOLS_ENABLED`, `DEBUG_ENABLED` | Full `EditorDock` suite, `GraphEdit` narrative editor, `EditorInspectorPlugin` pickers, HLOD cell baker, `EditorDebuggerPlugin`, custom `AnimationTrackEditor` UI, `DEV` terminal permissions, local debug `CommandBus` listener. |
| `target=template_debug` | `DEBUG_ENABLED` | Runtime `EngineDebugger` bridge (including live narrative graph trace), streaming/hydration telemetry overlays, `DEV` terminal permissions, opt-in hardened local `CommandBus` listener. |
| `target=template_release` | None | **Production hardened.** All remote debug listeners, `DEV` permission tiers, editor-only graph layout metadata, and editor hooks are stripped/compiled out via `#ifdef DEBUG_ENABLED` and `EditorExportPlugin`. |

**Coordinate Precision Build Option:** Default builds use standard `precision=single` paired with `OriginShifter` (Section 3.8.5) for optimal SIMD/GPU throughput. Projects opting for native Large World Coordinates may compile with `precision=double` (`REAL_T_IS_DOUBLE`), which automatically places `OriginShifter` into pass-through mode.

## 3. Core Subsystem Specifications

### 3.1 Multi-Clock Main Loop & Physics Determinism Boundary

Stock Godot's `Engine.time_scale` modifies `delta` globally without changing the real-time physics tick rate (causing collision precision loss when $\text{time\_scale} > 1.0$) and leaves audio unaffected. Furthermore, Godot's default **Jolt Physics** integration does **not** guarantee bit-exact or cross-platform determinism.

#### Specification

* **Tier 1 Patch (`main/main.cpp`):** Introduce three decoupled clocks managed by a C++ `SandboxClock` singleton:
  1. **Simulation Clock (`sim_clock`):** Drives `SimulationServer` at configurable fixed steps. Supports pause, single-step (`step(n)`), and high-speed fast-forward (up to $100\times$ simulated time) without altering 3D rigid-body physics or camera responsiveness.
  2. **Physics & World Clock:** Standard Godot `MainTimerSync` driving Jolt 3D physics and `_physics_process()`. Projects requiring smooth high-refresh rendering must enable **Physics Interpolation** and set `Engine.physics_jitter_fix = 0` when synchronizing with networked or deterministic systems.
  3. **Narrative & UI Clock:** Runs at unscaled real time (or independent cutscene scale) so diegetic 3D terminals, HUDs, and cinematics remain responsive at locked display refresh rates even when `sim_clock` is paused or fast-forwarding.
* **Determinism Boundary:** Deterministic state lives exclusively inside `SimulationServer` (pure math/data simulations). Jolt 3D physics is treated strictly as a visual/interactive physical representation layer, never as the authoritative source of deterministic simulation truth.

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

### 3.3 `EventBus` & `CommandBus` (`modules/sandbox_core`)

#### `EventBus` (C++ Singleton)

* Typed, decoupled publish-subscribe router bridging `SimulationServer`, `StreamingServer`, `HydrationBubble`, `MissionRunner`, 3D world props, and terminals.
* Supports main-thread deferred dispatch (`dispatch_deferred(event_id, payload)`) for events originating from background simulation or streaming threads, plus structured ring-buffer logging for Editor/Terminal inspection.

#### `CommandBus` (C++ Singleton)

* Single mutation entry point shared identically by in-game 3D terminals, the Godot Editor dock, automated test harnesses, and debug remote consoles.
* **Command Schema:** Every registered command defines:
  * `name: StringName` (namespaced, e.g., `sim.pause`, `state.set`, `event.fire`, `mission.start`, `stream.stats`, `bubble.pin`)
  * `min_permission: PermissionLevel` (`GUEST`, `USER`, `ADMIN`, `DEV`)
  * `arg_signature: Array[Variant.Type]` (strictly validated prior to execution)
  * `handler: Callable`
  * `help_text: String` (powers terminal autocomplete and `help` output)
* **Audit Logging:** Every execution returns a structured `CommandResult` (`ok: bool`, `output: String`, `error_code: Error`) and logs `(timestamp, caller_id, permission_level, command_string)`.

### 3.4 Domain-Agnostic Actor Contract (`SandboxActor` & `ActorRegistry`)

To keep cutscenes, missions, hydration pools, and terminals completely decoupled from host-project character controllers, any controllable entity (FPS player, 3rd-person rig, vehicle, RTS camera, NPC) attaches a `SandboxActor` child node.

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

* **Physics Interpolation Requirement:** Host controllers responding to `teleport_requested` (or `OriginShifter` world shifts) must invoke `reset_physics_interpolation()` on their root `Node3D` immediately after updating `global_transform` to prevent visual streaking between physics ticks.
* **Lifecycle:** `SandboxActor` auto-registers with `ActorRegistry` on `_enter_tree()` and unregisters on `_exit_tree()`.

### 3.5 Authored Sequences, Cutscenes, & Missions (`modules/sandbox_narrative`)

Because Godot's `AnimationTrackEditor` ($\approx 10,000$ lines of C++) is unexposed to `ClassDB`, extending `AnimationPlayer` with custom tracks requires engine-level C++ changes. The framework combines **native timeline tracks** for linear, frame-accurate cinematics with a **data-driven coroutine director** for dynamic, branching sequences.

#### Layer A: Native `AnimationPlayer` Tracks (Tier 1 Core Patch)

Patch `scene/resources/animation.h`, `animation_mixer.cpp`, and `editor/animation_track_editor.cpp` to add three native `Animation::TrackType` values:

* `TYPE_EVENT_BUS`: Fires typed `EventBus` signals at exact keyframe timestamps.
* `TYPE_WORLD_STATE`: Sets or interpolates `WorldState` variables directly along the timeline.
* `TYPE_ACTOR_COMMAND`: Dispatches `action_requested` or `control_lock_requested` to an `actor_id` via `ActorRegistry`.

This grants designers full timeline scrubbing, audio sync, Bézier curves, and collapsible node track summaries (Godot 4.7) with zero custom timeline UI code. Keyframe property editors for these tracks inherit the schema-driven searchable pickers specified in Section 3.7.3.

#### Layer B: `SequenceDirector` & `MissionRunner` (Tier 2 C++ Module)

For branching cinematics, dynamic mission objectives, and runtime conditions where linear timelines are too rigid:

* **`MissionData` (Resource):** Holds `mission_id`, `objectives: Array[ObjectiveData]`, `transitions: Array[MissionTransition]` (supporting linear chains, optional side objectives, fail branches, and converging paths), `on_start_sequence`, and `on_complete_sequence`.
* **`SequenceData` (Resource):** Holds `sequence_id`, an explicit node/step graph (`steps: Array[SequenceStep]`, supporting sequential awaits, parallel forks, join barriers, and conditional branches), and optional editor-only layout metadata (`_editor_graph_meta`, stripped on release export).
* **`SandboxCondition` (Abstract Resource):** `evaluate(state, event_id, payload)` returning `bool`. Framework ships generics (`CondStateEquals`, `CondStateRange`, `CondEventFired`, `CondAll`, `CondAny`, `CondNot`); host projects subclass for domain checks.
* **`SequenceAction` (Abstract Resource):** Asynchronous execution unit supporting sequential and parallel steps, leveraging Godot 4.7's `Tween.tween_await()` for clean non-blocking waits. Framework ships `ActionPlayAnimation`, `ActionSetState`, `ActionEmitEvent`, `ActionRunCommand`, `ActionLockActor`, `ActionBranchCondition`, and `ActionWaitEvent`.
* **Camera Interop:** Camera blending remains decoupled from the core module; host projects may bind `ActionBlendCamera` to standard `Camera3D` tweens or community addons such as **Phantom Camera**.

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
5. **FSR2 Guardrail:** Terminal viewports must maintain a minimum resolution of at least $256 \times 256$ to avoid known FSR2 compute-job errors on tiny viewports.

#### Input Forwarding & Terminal Focus Mode

* **3D-to-2D Coordinate Synthesis:** An `Area3D` collider aligned to the terminal screen intercepts raycasts, transforms the hit point into local quad space (`quad.global_transform.affine_inverse()`), normalizes UV coordinates to `SubViewport.size`, synthesizes `position`, `relative`, and `velocity`, and dispatches via `SubViewport.push_input()`.
* **Captured Mouse Handling:** Because `Input.MOUSE_MODE_CAPTURED` (standard in FPS games) locks `InputEventMouseMotion.position` to screen center and zeroes `screen_velocity`, `TerminalDisplay3D` uses explicit camera-center raycasting rather than cursor physics picking while in captured mode.
* **Tier 1 Patch — Native Terminal Focus Mode:** When a player engages a terminal, `TerminalDisplay3D` locks the player's `SandboxActor` (`control_lock_requested(true, &"terminal")`) and activates a native `Viewport` focus override that routes keyboard and scroll events directly into the target `SubViewport` until an escape key (`ui_cancel`) releases focus.

#### Permission Model

Each `TerminalDisplay3D` exports a `permission_tier` (`GUEST`, `USER`, `ADMIN`, `DEV`) and an optional `allowed_namespaces: Array[StringName]` whitelist (e.g., `[&"power", &"doors"]`). Diegetic in-world terminals can only execute commands matching both their tier and namespace whitelist; `DEV` tier is compiled out in `template_release`.

### 3.7 Godot Editor Integration & Authoring Tooling (`modules/sandbox_editor`)

To eliminate Inspector fatigue on multi-branch missions and prevent silent typo bugs when wiring systemic events, `modules/sandbox_editor` provides a unified authoring suite built on native Godot 4.7 editor APIs.

#### 3.7.1 Modern Dock Architecture (`EditorDock`)

Uses Godot 4.6+'s movable, floatable **`EditorDock`** API (`EditorPlugin.add_dock()`), avoiding deprecated legacy bottom-panel APIs. The Sandbox Dock hosts five tabs (detachable onto secondary monitors):

* **Asset Browser Tab:** Scans `res://` for `SimModule`, `MissionData`, `SequenceData`, `SandboxSchemaRegistry`, and `WorldPartitionManifest` `.tres` assets for rapid inspection and creation.
* **Narrative Flow Graph Tab:** Visual `GraphEdit` canvas for authoring `MissionData` and `SequenceData` assets (detailed in Section 3.7.2).
* **Live WorldState & Streaming Inspector:** Displays real-time SoA variables, active missions, `SimModule` tick timings (microseconds per tick), loaded cell heatmaps, VRAM residency, and hydration bubble counts.
* **HLOD Baker Utility:** Editor-side tool to partition open-world scenes into `WorldPartitionCell` `.scn` bundles and bake merged static HLOD proxy meshes plus texture atlases.
* **Command Console:** Executes `CommandBus` commands directly against the running game instance with full autocomplete backed by `CommandBus` help metadata.

#### 3.7.2 Visual Mission & Sequence Editor (`GraphEdit` Tab)

Stock Inspector arrays become difficult to read once a `MissionData` or `SequenceData` resource exceeds $8\text{–}10$ steps or nests boolean condition combinators (`CondAll`/`CondAny`). The **Narrative Flow Graph** tab wraps Godot's native `GraphEdit` and `GraphNode` controls to provide direct visual editing over `.tres` resources with **zero separate compilation step**.

* **Single Source of Truth:** Opening any `MissionData` or `SequenceData` `.tres` file in the FileSystem dock or Inspector binds the `GraphEdit` canvas directly to that live `Resource` instance. Adding nodes, rewiring ports, or editing inline fields mutates the underlying resource directly.
* **Typed Port System & Node Palette:** Ports enforce connection type safety via `GraphEdit::add_valid_connection_type(from_type, to_type)`:
  * *Port Type `0` (Execution Flow — White/Blue):* Connects sequential steps, mission entry points, objective completion/failure transitions, and parallel fork/join barriers.
  * *Port Type `1` (Condition Logic — Amber):* Connects `SandboxCondition` leaf nodes into `CondAll`, `CondAny`, or `CondNot` combinator nodes and feeds into `ObjectiveNode` or `BranchNode` gates.
  * *Port Type `2` (Sequence Reference — Purple):* Links `MissionData` lifecycle hooks (`on_start_sequence`, `on_complete_sequence`) to reusable `SequenceData` sub-assets.
* **Standard Node Types:**
  * `MissionEntryNode`: Entry root exposing `mission_id`, prerequisite conditions, and `On Start` / `First Objective` outputs.
  * `ObjectiveNode`: Displays objective title, `Required` vs. `Optional` toggle, Condition Logic input port, and `On Complete` / `On Fail` execution outputs.
  * `ConditionNode` / `CombinatorNode`: Renders inline schema-picked `WorldState` or `EventBus` condition summaries (`power.sector_a_load > 90.0`) or boolean gates (`ALL`, `ANY`, `NOT`).
  * `SequenceActionNode`: Auto-discovered via `ClassDB` for every engine and host-project `SequenceAction` subclass. Visually distinguishes blocking steps (`wait_for_completion = true`, solid execution wire) from fire-and-forget parallel actions (`wait_for_completion = false`, dashed side-branch wire).
  * `WaitEventNode` / `BranchNode`: Pauses sequence flow until a schema-selected `EventBus` trigger fires or routes execution across `True` / `False` branches based on a connected condition.
  * `CommentFrameNode` (`GraphFrame`): Groups related mission beats or set-piece phases for readability.
* **Editor Integration & Version-Control Hygiene:**
  * **Undo/Redo Parity:** Every graph mutation (add/delete node, connect/disconnect port, drag move, property change) registers an atomic action on `EditorUndoRedoManager`, keeping the `GraphEdit` canvas and the Godot Inspector 100% synchronized.
  * **Clean Git Diffs & Auto-Layout:** Node 2D canvas positions (`Vector2`) and `GraphFrame` rectangles are stored in a dedicated `_editor_graph_meta: Dictionary` keyed by stable node UUIDs (sorted deterministically on save to minimize Git merge conflicts) and stripped automatically from exported builds via an `EditorExportPlugin`. An **Auto-Arrange (Layered DAG)** toolbar button cleanly lays out `.tres` resources authored manually in the Inspector or via external scripts.
* **Live Runtime Execution Highlighting:** During an active debug session, `MissionRunner` and `SequenceDirector` stream lightweight state transitions over `EngineDebugger` (`sandbox:narrative_trace`). The `GraphEdit` canvas highlights currently active objectives in **green**, awaiting/blocked `SequenceAction` steps in **amber**, completed paths in **dimmed cyan**, and failed branches in **red**, allowing designers to watch live systemic missions execute in real time.

#### 3.7.3 Schema-Driven `EditorInspectorPlugin` & Searchable ID Pickers

Manually typing raw `StringName` keys (`&"power.sector_a_load"`, `&"npc_guard_01"`) into Inspector fields is error-prone and produces silent runtime mismatches. `modules/sandbox_editor` registers a native C++ `EditorInspectorPlugin` backed by a project-wide **`SandboxSchemaRegistry`**.

* **`SandboxSchemaRegistry` (`modules/sandbox_core/`):**
  * A project-configured `.tres` manifest (referenced via `ProjectSettings`: `sandbox/schema/registry_path`) storing typed descriptors for:
    * **WorldState Keys:** `key: StringName`, `value_type: Variant.Type`, `default_value: Variant`, `range_hint: Vector2` (optional min/max), `deprecated: bool`, `description: String`.
    * **EventBus Events:** `event_id: StringName`, `payload_schema: Dictionary` (`field_name -> Variant.Type`), `deprecated: bool`, `description: String`.
    * **Actor & Tag Catalog:** Known global `actor_id`s, `SandboxActor` group `tags`, and `HydratableActor` `archetype_id`s.
  * **Dynamic Discovery Merge:** In the Editor, `SandboxSchemaRegistry` merges static manifest entries with auto-discovered keys declared by loaded `SimModule` scripts (`get_declared_schema()`) and all `SandboxActor` / `Marker3D` nodes present in the currently open 3D scene.
* **Property Hint Interception Contract:** Any engine C++ class or host-project GDScript/C# `Resource` or `Node` opts into the picker via standardized export annotations:

```gdscript
# Host-project or engine resource using custom Inspector pickers
@export_custom(PROPERTY_HINT_NONE, "sandbox_world_state_key:float") var target_key: StringName
@export_custom(PROPERTY_HINT_NONE, "sandbox_event_id") var trigger_event: StringName
@export_custom(PROPERTY_HINT_NONE, "sandbox_actor_id") var target_actor: StringName
@export_custom(PROPERTY_HINT_NONE, "sandbox_marker_id") var camera_anchor: StringName
```

* **Searchable `EditorProperty` Dropdown Control:**
  * Replaces default `LineEdit` widgets inside both the Godot Inspector and `AnimationTrackEditor` keyframe panels with a custom `SandboxIdPickerProperty`.
  * Clicking the field opens a lightweight fuzzy-searchable popup (`PopupPanel` + `LineEdit` filter + `Tree`) grouped hierarchically by dot-namespace (e.g., `weather` $\rightarrow$ `weather.rain_intensity`, `power` $\rightarrow$ `power.sector_a_load`).
  * Displays each entry's `Variant.Type` badge, documentation tooltip, and optional type-filter enforcement (e.g., `sandbox_world_state_key:float` surfaces numeric keys first and warns if a `String` key is bound to a numeric range condition).
  * **Inline Validation Badges:** Renders a subtle warning/error icon directly on the Inspector row if a referenced key is missing from `SandboxSchemaRegistry`, marked `deprecated`, or fails a `Variant.Type` compatibility check.
* **3D Scene Picking & Focus Navigation:**
  * For `sandbox_actor_id` and `sandbox_marker_id` fields, `SandboxIdPickerProperty` renders two compact inline icon buttons beside the dropdown:
    1. **Pick from 3D Scene (Eyedropper):** Enters a one-click viewport picking mode (or opens a filtered list of `SandboxActor` / `Marker3D` nodes in the active scene tree) to populate the ID without switching tabs or copy-pasting strings.
    2. **Focus in 3D Viewport:** Selects and frames the matching `SandboxActor` or `Marker3D` in the active 3D Editor viewport if present.
* **Strict vs. Permissive Authoring Mode:** Configurable via `ProjectSettings` (`sandbox/schema/strict_mode`):
  * *Permissive (Default during prototyping):* Designers may type ad-hoc unregistered keys directly into the picker search box (displaying a soft yellow warning badge and a one-click **"Add to Schema Registry"** button).
  * *Strict (Production / CI Gate):* Disallows unregistered keys and runs an automated pre-export schema audit across all `MissionData`, `SequenceData`, and `Animation` `.tres` files, failing export if broken references exist.

#### 3.7.4 Bidirectional Runtime IPC (`EditorDebuggerPlugin`)

Uses `EditorDebuggerPlugin` (Editor side) paired with `EngineDebugger` (Game side):

* *Editor to Game:* Commands are sent via `EditorDebuggerSession.send_message("sandbox:cmd", [raw_cmd])` and received via `EngineDebugger.register_message_capture("sandbox", ...)`.
* *Game to Editor:* State snapshots, profiler metrics (`profiler_add_frame_data()`), streaming telemetry, narrative execution traces (`sandbox:narrative_trace`), and event logs stream back via `EngineDebugger.send_message("sandbox:telemetry", [...])`.

### 3.8 Spatial Partitioning, Per-LOD Async Streaming, HLOD, & Floating Origin (`modules/sandbox_streaming`)

Stock Godot 4.7 lacks built-in world partitioning, and its native Auto-LOD system only toggles mesh visibility while keeping all LOD levels, textures, and Jolt collision shapes resident in RAM and VRAM. Furthermore, single-precision floating-point coordinates degrade visibly beyond $\approx 4\text{ km}$ from the world origin. `modules/sandbox_streaming` is an opt-in C++ engine module that solves these limitations for systemic open worlds and dense urban sandboxes.

#### 3.8.1 Hierarchical Spatial Grid (`WorldPartitionGrid`)

* **Multi-Tier Cell Layers:** Worlds are indexed by a `WorldPartitionManifest` resource defining one or more spatial grid layers tailored to content density:
  * *Macro Layer (e.g., $512\text{ m} \times 512\text{ m}$ cells):* Terrain patches, skyline landmarks, major highway segments, HLOD1 proxies.
  * *Meso Layer (e.g., $128\text{ m} \times 128\text{ m}$ cells):* City blocks, outposts, building exteriors, HLOD0 proxies.
  * *Micro Layer (e.g., $32\text{ m} \times 32\text{ m}$ cells):* Interiors, dense street clutter, destructible props, detailed Jolt static colliders.
* **Streaming Invokers:** Cells are requested by one or more registered `StreamingInvoker` anchors (typically the active camera/player, plus temporary pins from `SequenceDirector` for cutscenes or sniper scopes).
* **Velocity-Predictive Lookahead:** Each invoker shifts its streaming evaluation frustum and radius forward along its smoothed velocity vector $\vec{v}$ by a configurable lookahead horizon $\Delta t_{\text{lookahead}}$ (e.g., $2.0\text{ s}$–$4.0\text{ s}$), ensuring cells ahead of high-speed vehicles or aircraft begin loading well before visual pop-in.

#### 3.8.2 True Per-LOD Asynchronous Resource Streaming

* **Residency Decoupled from Visibility:** Instead of packing all LOD tiers into one monolithic scene, each cell manifest references discrete per-LOD resource bundles (`hlod_impostor_path`, `mid_mesh_path`, `high_scene_path`, `collision_bundle_path`).
* **State Machine per Cell:** Each cell transitions cleanly across five explicit residency states:
  `UNLOADED` $\leftrightarrow$ `LOADING_ASYNC` $\leftrightarrow$ `STAGED` $\leftrightarrow$ `ACTIVE_ATTACHED` $\leftrightarrow$ `EVICTING`
* **True RAM/VRAM Reclamation:** When a cell leaves its eviction radius (with hysteresis margin $R_{\text{evict}} > R_{\text{load}}$ to prevent boundary thrashing), `StreamingServer` detaches the cell's nodes, unregisters its Jolt collision shapes, drops all internal `Ref<Resource>` handles, and triggers deferred VRAM cleanup so distant high-detail assets do not exhaust memory budgets.
* **Priority Queue:** Async load requests (`ResourceLoader::load_threaded_request`) are strictly prioritized by:
  $$\text{Priority} = w_1 \cdot \frac{1}{\max(d, 1.0)} + w_2 \cdot \max(0, \hat{f} \cdot \hat{d}_{\text{cell}}) + w_3 \cdot \text{PinWeight}$$
  where $d$ is distance to the nearest invoker and $\hat{f} \cdot \hat{d}_{\text{cell}}$ biases cells directly in front of the camera or velocity heading.

#### 3.8.3 Time-Sliced Main-Thread Activation Budget

Because Godot's `SceneTree` attachment and Jolt body insertion must execute on the main thread, bulk-activating a dense city block in a single frame causes severe frame hitches.

* **Hard Frame Budget:** `StreamingServer` enforces a configurable main-thread time budget (default $B_{\text{stream}} \le 2.0\text{ ms}$ per frame, dynamically reducible under heavy GPU/CPU load).
* **Incremental Staged Activation:** Loaded scenes in the `STAGED` queue are unpacked incrementally across multiple frames:
  1. Register static Jolt collision shapes (prioritized first so fast-moving players never fall through streamed ground).
  2. Attach visual `Node3D` sub-batches and warm PSO/shader caches.
  3. Notify `EventBus` via `&"stream_cell_activated"` (`{cell_id, layer}`) once complete.

#### 3.8.4 HLOD (Hierarchical Level of Detail) Mesh Merging & Swapping

* **Offline Bake Pipeline (`sandbox_editor`):** Designers tag static architecture and terrain props per cell. The Editor HLOD baker merges static geometry within each Meso/Macro cell into a single combined 3D proxy mesh (`SurfaceTool` / `ArrayMesh`) with a rect-packed albedo/normal/ORM texture atlas (`Image`), reducing hundreds of draw calls per city block to **1 draw call** while preserving real 3D lighting and shadows from all viewing angles.
* **Seamless Runtime Transition:**
  * Beyond the Meso load distance, only the baked HLOD proxy is resident and visible.
  * When the live Meso cell completes `ACTIVE_ATTACHED`, `StreamingServer` runs a brief dithered cross-fade ($0.2\text{ s}$–$0.35\text{ s}$ via shader instance uniform `hlod_fade`) before disabling and optionally unloading the HLOD proxy, eliminating sudden pop-in or visual gaps.

#### 3.8.5 Large-World Precision: Floating Origin vs. Double Precision

To eliminate floating-point transform jitter, Z-fighting, and Jolt physics instability on maps larger than $4\text{ km} \times 4\text{ km}$:

* **Default Strategy (`precision=single` + `OriginShifter`):**
  * `WorldState`, `SimulationServer`, and `WorldPartitionGrid` store authoritative entity/cell coordinates in **64-bit double precision** (`WorldPos64` = `Vector3d` or `Vector3i` sector + local `Vector3`).
  * Active `SceneTree` rendering and Jolt physics operate in fast 32-bit local coordinates relative to `current_origin_world`.
  * When the primary invoker drifts farther than a configurable threshold $R_{\text{shift}}$ (default $2048\text{ m}$) from `(0, 0, 0)`, `OriginShifter` executes an atomic shift at the start of the physics tick:
    1. Snaps the shift delta $\Delta \vec{O}$ to a clean integer grid multiple (e.g., $512\text{ m}$) to keep texel and shadow-cascade alignment deterministic.
    2. Translates all registered world-root nodes, active Jolt bodies, navigation maps, particles, and active camera offsets by $-\Delta \vec{O}$.
    3. Immediately calls `reset_physics_interpolation()` on all shifted `Node3D`s to prevent 1-frame motion blur or interpolation streaking.
    4. Emits `EventBus` signal `&"origin_shifted"` with `{"delta": delta_vec3, "new_origin_64": origin_64}` so host-project VFX or custom shaders can adjust.
* **Alternative Strategy (`precision=double`):** Supported cleanly via SCons `precision=double` (`REAL_T_IS_DOUBLE`). When enabled, `OriginShifter` acts as a no-op pass-through; shaders still rely on Godot's camera-relative view matrices to keep GPU vertex math within 32-bit precision.

#### 3.8.6 Streaming `CommandBus` Surface

`sandbox_streaming` automatically registers diagnostic and control commands:
* `stream.stats` — Outputs resident cell count, async queue depth, main-thread activation ms, and estimated VRAM budget.
* `stream.budget <ms>` — Adjusts the per-frame main-thread activation budget live.
* `stream.pin <layer> <x> <z>` / `stream.unpin <layer> <x> <z>` — Pins or releases a specific cell (useful for cutscenes and testing).
* `stream.hlod_force <0|1|auto>` — Forces HLOD proxies on/off for visual inspection.

### 3.9 Simulation-to-Node Actor Hydration Bubble (`modules/sandbox_bubble`)

In dense urban sandboxes (hundreds of cars, thousands of pedestrians) or large systemic worlds (roaming patrols, wildlife herds, convoys), instantiating full `Node3D` + Jolt rigid-body hierarchies across the entire map destroys frame rates. `modules/sandbox_bubble` bridges `SimulationServer`'s contiguous SoA memory with Godot's live `SceneTree` via a pooled, distance-tiered **Hydration Bubble**.

#### 3.9.1 Three-Tier Entity Representation

Every hydratable agent registered with `SimulationServer` transitions seamlessly across three fidelity tiers based on distance to active invokers:

| Tier | Typical Range | Representation | CPU / Physics Cost | Visuals |
| :--- | :--- | :--- | :--- | :--- |
| **Tier 0: Abstract SoA** | $d > R_{\text{proxy}}$ (e.g., $> 400\text{ m}$) | Contiguous C++ structs in `SimulationServer` (`pos64`, `vel`, `rot`, `archetype_id`, `state_flags`, `health`, `spline_cursor`) | Ticked on `WorkerThreadPool` at $1\text{–}5\text{ Hz}$; zero `SceneTree` nodes, zero Jolt bodies | None (outside draw distance or occluded) |
| **Tier 1: Batched Proxy Ring** | $R_{\text{out}} < d \le R_{\text{proxy}}$ (e.g., $165\text{ m}\text{–}400\text{ m}$) | Same SoA struct + direct GPU transform upload via `RenderingServer::multimesh_set_buffer()` | Zero `SceneTree` nodes, zero Jolt bodies; flat memcpy/transform pack per archetype | Low-LOD `MultiMeshInstance3D` (distant traffic silhouettes, headlights, marching patrols) |
| **Tier 2: Hydrated Live Actor** | $d \le R_{\text{in}}$ (e.g., $\le 140\text{ m}$) | Pre-warmed `Node3D` hierarchy claimed from `ActorNodePool` and bound to the entity's `RID` | Full Jolt physics, `SandboxActor` signals, skeletal animation, audio, host-project AI/vehicle controllers | Full-detail rigged meshes, dynamic lights, physics collisions |

#### 3.9.2 Bubble Geometry, Hysteresis, & Velocity Bias

* **Hysteresis Dead-Zone:** To prevent entities near the bubble edge from rapidly oscillating between Tier 1 and Tier 2, hydration enforces distinct enter and exit radii:
  $$R_{\text{in}} < R_{\text{out}} \quad (\text{default: } R_{\text{in}} = 140\text{ m},\ R_{\text{out}} = 165\text{ m})$$
* **Velocity-Biased Ellipsoid:** When an invoker moves at speed $v$, the bubble center shifts forward along $\vec{v}$ by $\min(v \cdot t_{\text{lead}}, d_{\text{max}})$, prioritizing ahead-of-travel traffic and encounters while accelerating behind-camera dehydration.
* **Archetype-Specific Radii:** Host projects may configure custom radii per archetype (e.g., helicopters hydrate at $350\text{ m}$; street pedestrians hydrate at $90\text{ m}$).

#### 3.9.3 `ActorNodePool` & Time-Sliced Hydration Budget

Runtime `PackedScene.instantiate()` and `queue_free()` calls are major sources of GC/allocator hitching.

* **Pre-Warmed Pools:** On world startup, `ActorNodePool` pre-instantiates a configurable pool of inactive `Node3D` shells per `archetype_id` (e.g., 40 sedan rigs, 80 pedestrian rigs, 12 patrol vehicles). Inactive pooled nodes remain detached from physics processing (`PROCESS_MODE_DISABLED`, Jolt bodies sleeping/disabled in collision layers `0`).
* **Per-Frame Cap:** `HydrationBubble` enforces a strict per-frame budget (default: max $4$ hydrations and $6$ dehydrations per frame, or $\le 1.5\text{ ms}$ total main-thread time). Closest and in-frustum candidates are hydrated first.
* **Pool Exhaustion Policy:** If an archetype pool reaches $100\%$ capacity inside a dense crowd, `HydrationBubble` either (a) keeps excess distant candidates in Tier 1 `MultiMesh` proxy mode or (b) recycles the farthest behind-camera non-pinned Tier 2 actor, depending on the archetype's configured overflow policy.

#### 3.9.4 Authoritative State Round-Trip & Pinning

State must never be lost when an entity crosses the bubble boundary (e.g., a damaged police car driving out of range and returning 10 seconds later must remain damaged).

* **Hydration (Tier 0/1 to Tier 2):**
  1. Claim pooled node for `archetype_id` and bind `entity_rid`.
  2. Convert `pos64` to local floating-origin `Transform3D` and set `global_transform`.
  3. Invoke `reset_physics_interpolation()` on the pooled root node.
  4. Copy SoA payload (velocity, health, fuel, faction, AI intent, custom byte blob) via `on_hydrate(rid, soa_data)`.
  5. Re-enable physics/processing and clear the entity's Tier 1 `MultiMesh` slot.
* **Dehydration (Tier 2 to Tier 1/0):**
  1. Check `HYDRATION_PIN` bitmask. Entities pinned by `MissionRunner`, `SequenceDirector`, active player occupancy (e.g., player driving the car), or active combat lock are **exempt** from dehydration until unpinned.
  2. Invoke `on_dehydrate(rid) -> PackedByteArray` to flush live transform (converted back to `pos64`), velocity, damage, and state flags back into `SimulationServer` SoA memory.
  3. Disable Jolt bodies/processing and return the node cleanly to `ActorNodePool`.
* **Host Project Contract (`HydratableActor`):**

```gdscript
# Exposed C++ Node/Component paired with SandboxActor on pooled prefabs
class_name HydratableActor
extends Node

@export var archetype_id: StringName
@export var hydrate_radius: float = 140.0
@export var dehydrate_radius: float = 165.0

var bound_rid: RID

# Overridden or connected by host project prefabs
signal hydrated(entity_rid: RID, initial_state: Dictionary)
signal dehydrating(entity_rid: RID, out_state: Dictionary)

func set_pinned(pinned: bool, reason: StringName) -> void:
	HydrationBubble.set_entity_pin(bound_rid, pinned, reason)
```

#### 3.9.5 Hydration `CommandBus` Surface

* `bubble.stats` — Displays entity counts across Tier 0 (SoA), Tier 1 (`MultiMesh`), and Tier 2 (Hydrated), plus per-archetype pool utilization.
* `bubble.radius <archetype> <in_m> <out_m>` — Live-tunes hydration radii for performance profiling.
* `bubble.pin <rid>` / `bubble.unpin <rid>` — Locks or unlocks an entity in Tier 2 for debugging.

## 4. Serialization, Save/Load, & Time-Scrubbing

Godot's native `.tres`/`.res` formats (`ResourceSaver`) can embed executable scripts, and binary deserializers with `allow_objects = true` permit arbitrary code execution. **Using `ResourceSaver` or object deserialization for player save files is strictly prohibited.**

### 4.1 Hardened Save Format Specification

* **Pre-Save Hydration Flush:** Before capturing any save snapshot, `HydrationBubble` performs a synchronous read-only state flush from all currently active Tier 2 hydrated actors into `SimulationServer` SoA memory. This guarantees that save files are 100% independent of which nodes happened to be hydrated at the moment of saving.
* **Sparse Cell Diffs:** `StreamingServer` persists only mutated world-cell state (e.g., destroyed props, opened gates, burned foliage bitmasks) as compact per-cell sparse diffs rather than serializing static geometry.
* **Allowed Formats:**
  * **Binary Snapshot (`.sbsav`):** Used for rapid autosaves, network sync, and in-memory rewind ring buffers. Encoded via custom C++ `StreamPeerBuffer` writing strictly primitive types, fixed headers, 64-bit world origin coordinates, and `PackedByteArray` blobs from `SimulationServer` SoA memory.
  * **Structured JSON (`.json`):** Used for human-readable exports, modding, or bug reports.
* **Deserializer Guardrail:** All calls to `bytes_to_var()`, `FileAccess.get_var()`, `PacketPeer.get_var()`, `StreamPeer.get_var()`, and `Marshalls.base64_to_variant()` across the entire codebase must strictly enforce `allow_objects = false`.
* **Schema Envelope:**

```json
{
  "schema_version": 2,
  "engine_version": "4.7.2-sandbox",
  "sim_tick_index": 14820,
  "sim_time_elapsed": 7410.0,
  "world_origin_64": [4096.0, 0.0, -2048.0],
  "world_state": {
    "weather.rain_intensity": 0.42,
    "power.sector_a_load": 88.5
  },
  "sim_modules": {
    "economy": "<base64_primitive_blob>",
    "traffic_soa": "<base64_primitive_blob>"
  },
  "streaming_cell_diffs": {
    "meso_8_-4": "<base64_destroyed_prop_bitmask>"
  },
  "missions": {
    "active": ["heist_01"],
    "completed": ["intro_00"],
    "objective_progress": {}
  }
}
```

### 4.2 Deterministic Simulation Rewind (Optional Debug/Gameplay Feature)

Because `SimulationServer` stores state in contiguous C++ SoA arrays, the server maintains an optional ring buffer of compressed binary snapshots taken every $N$ simulation ticks. Commands such as `sim.rewind <ticks>` restore `WorldState`, force `HydrationBubble` to resync active Tier 2 actors from the restored SoA state (calling `reset_physics_interpolation()`), and emit a `world_state_restored` event on `EventBus`.

## 5. Security & Hardening Specification

| Threat Vector | Affected Component | Mandatory Mitigation |
| :--- | :--- | :--- |
| **Deserialization RCE** | Save/Load, Modding, Network Sync | Ban `ResourceSaver`/`ResourceLoader` for untrusted saves; enforce `allow_objects = false` on all Godot Variant deserializers; validate schema versions and numeric bounds before applying state. |
| **Chunk Manifest Path Traversal** | `StreamingServer` (`WorldPartitionManifest`) | Restrict all streamed cell and HLOD resource paths strictly to normalized `res://` or signed/mounted PCK paths; reject `..` traversal, `user://` executable scripts, and absolute filesystem paths. |
| **Cross-Site WebSocket Hijacking (CSWSH)** | Local Debug `CommandBus` Server (TCP/WS) | Strictly validate HTTP `Origin` header against an explicit allowlist (reject browser origins); require a cryptographically random per-session Bearer token/nonce generated at engine startup and passed out-of-band. |
| **DNS Rebinding & `0.0.0.0` Bypass** | Local Debug `CommandBus` Server | Bind strictly to `127.0.0.1` / `::1`; validate `Host` header strictly against `localhost` or `127.0.0.1`; explicitly reject `0.0.0.0/8` requests. |
| **Debug Surface Leakage in Shipping Builds** | `CommandBus` Server, `DEV` Terminals | Wrap all remote debug server code and `DEV` command registrations inside `#ifdef DEBUG_ENABLED`. Verify zero symbols/listeners exist in `target=template_release` binaries via automated CI checks. |
| **Command Injection / DoS** | Diegetic 3D Terminals | Enforce strict `PermissionLevel` and namespace whitelists per terminal instance; cap input buffer length (e.g., 512 bytes) and rate-limit command execution per second. |

## 6. Verification & Acceptance Criteria

Before a framework milestone is marked production-ready, it must pass the following automated and manual verification gates:

1. **Multi-Clock Isolation Test:** Run `sim_clock` at $50\times$ speed and paused ($0\times$) while verifying that Jolt 3D physics maintains $60\text{ Hz}$ stability, audio playback speed remains $1.0$, and 3D terminal input latency stays below $16.6\text{ ms}$.
2. **Thread Safety & Leak Audit:** Execute $10,000$ consecutive `SimulationServer` ticks across $8$ concurrent `SimModule`s on `WorkerThreadPool` under AddressSanitizer / ThreadSanitizer with zero data races, zero `ERR_BUSY` aborts, and zero leaked task handles.
3. **Domain Agnosticism Proof:** Mount the compiled engine onto two distinct test harness projects—(a) a 1st-person `CharacterBody3D` scene and (b) an RTS top-down camera scene—executing the exact same `MissionData`, `AnimationPlayer` cutscene, and `TerminalDisplay3D` interaction without modifying engine code.
4. **3D Terminal Legibility & Performance Gate:** Render $4$ simultaneous `TerminalDisplay3D` monitors in a 3D room; verify readable MSDF text at $1\text{ m}$–$8\text{ m}$ distances and $45^\circ$ viewing angles, confirm `disable_3d = true` eliminates duplicate occlusion culling passes, and verify captured-mouse interaction plus `ui_cancel` focus release.
5. **Security Verification:**
   * Attempt to load a crafted save file containing an embedded GDScript `Object` payload; confirm safe rejection with `ERR_INVALID_DATA`.
   * Attempt to register a `WorldPartitionManifest` containing `../` or `user://` script paths; confirm immediate rejection.
   * Attempt an unauthenticated or mismatched-`Origin` WebSocket handshake against the debug `CommandBus` port; confirm termination with HTTP `403 Forbidden`.
   * Audit `target=template_release` export; confirm no listening debug sockets open and `DEV` commands return `ERR_UNAUTHORIZED` / `Unknown command`.
6. **High-Speed Open-World Streaming & Floating Origin Gate (`sandbox_streaming`):**
   * Traverse a $16\text{ km} \times 16\text{ km}$ partitioned test world at $90\text{ m/s}$ ($324\text{ km/h}$) across at least $4$ `OriginShifter` thresholds ($R_{\text{shift}} = 2048\text{ m}$).
   * Verify 99th-percentile frame time remains within target budget ($\le 16.6\text{ ms}$ at 60 FPS), main-thread streaming activation stays $\le 2.0\text{ ms/frame}$, VRAM returns to baseline after leaving dense sectors (confirming true per-LOD unloading), HLOD cross-fades exhibit zero missing-geometry frames, and origin shifts produce zero physics interpolation streaks.
7. **Hydration Bubble SoA-to-Node Stress Gate (`sandbox_bubble`):**
   * Simulate $2,000$ active SoA traffic/pedestrian agents in `SimulationServer` while moving a streaming invoker through a dense district.
   * Verify zero runtime `instantiate()` / `queue_free()` calls during steady-state traversal (100% pool reuse), zero oscillation/thrashing at the $R_{\text{in}} / R_{\text{out}}$ boundary, intact state round-trip fidelity (damage, health, velocity, and `HYDRATION_PIN` preservation) across $500$ hydrate/dehydrate cycles, and lossless save/load restoration after a pre-save hydration flush.
8. **Visual Authoring & Schema Validation Gate (`sandbox_editor`):**
   * Author a $15$-node branching `MissionData` + `SequenceData` graph (including `CondAll`/`CondAny` combinators, parallel `SequenceAction` forks, and fail transitions) purely inside the `GraphEdit` tab without manually editing Inspector arrays.
   * Verify atomic `EditorUndoRedoManager` rollback across node/port edits, real-time synchronization with the Inspector, automatic stripping of `_editor_graph_meta` in `target=template_release` exports, and live node-state highlighting over `EditorDebuggerPlugin` during gameplay.
   * Verify `EditorInspectorPlugin` fuzzy search, namespace grouping, `Variant.Type` mismatch / missing-key badges, strict-mode pre-export audit failure on broken references, and 3D viewport eyedropper picking for `SandboxActor` and `Marker3D` IDs across both the Inspector and custom `AnimationTrackEditor` keyframes.