# Modular 3D Sandbox Framework — Implementation Status

| Attribute | Value |
| :--- | :--- |
| **Specification Baseline** | [SPEC.md](./SPEC.md) (Draft v1.2 — Visual Narrative Authoring, Schema Pickers, Open-World Streaming & Hydration Bubble) |
| **Scope & Constraints** | [SCOPE.md](./SCOPE.md) (8-Phase Work Breakdown & Codebase Audit Findings 1–10) |
| **Current Phase** | **Phase 1 — `modules/sandbox_core` & Tier 1 `SandboxClock` Hook** (Ready to Start) |
| **Overall Progress** | `0 / 8` Implementation Phases Complete · `0 / 8` Verification Gates Passed |

---

## 1. Phase Summary

| Phase | Subsystem / Target | Tier | Status | Verification Gate |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 1** | `modules/sandbox_core` & `main/main.cpp` (`SandboxClock`) | Tier 1 & 2 | `[ ] Not Started` | Gate 1, Gate 5 |
| **Phase 2** | `modules/sandbox_sim` (`SimulationServer`, `SimModule`, Rewind) | Tier 2 | `[ ] Not Started` | Gate 2 |
| **Phase 3** | `modules/sandbox_narrative` & `Animation` Custom Tracks | Tier 1 & 2 | `[ ] Not Started` | Gate 3 |
| **Phase 4** | `modules/sandbox_streaming` (`WorldPartitionGrid`, HLOD, `OriginShifter`) | Tier 2 (Opt-In) | `[ ] Not Started` | Gate 5, Gate 6 |
| **Phase 5** | `modules/sandbox_bubble` (`HydrationBubble`, `ActorNodePool`, `MultiMesh`) | Tier 2 (Opt-In) | `[ ] Not Started` | Gate 7 |
| **Phase 6** | `modules/sandbox_terminal` & `Viewport` Native Terminal Focus | Tier 1 & 3 | `[ ] Not Started` | Gate 4 |
| **Phase 7** | `modules/sandbox_editor` (`EditorDock`, `GraphEdit`, Schema Pickers, HLOD Baker) | Tier 3 | `[ ] Not Started` | Gate 8 |
| **Phase 8** | End-to-End Verification Gates & Domain Test Harnesses | Tier 4 | `[ ] Not Started` | Gates 1–8 |

---

## 2. Detailed Implementation Checklist

### Phase 1: `modules/sandbox_core` & Tier 1 `SandboxClock` Main-Loop Hook
- [ ] `modules/sandbox_core/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_core/sandbox_clock.{h,cpp}` (`sim_clock`, `physics_clock`, `narrative_ui_clock`, spiral-of-death frame budget cap)
- [ ] `modules/sandbox_core/world_state.{h,cpp}` (Named Scalar SoA Channel Bank + `RID` Entity SoA Tables with structural lock/commit)
- [ ] `modules/sandbox_core/sandbox_schema_registry.{h,cpp}` (`SandboxSchemaRegistry` `.tres` manifest, dynamic discovery merge, type/deprecation validator)
- [ ] `modules/sandbox_core/event_bus.{h,cpp}` (Pub-sub router, thread-aware `emit()` -> mutex-guarded `dispatch_deferred` ring buffer, main-thread flush, history log)
- [ ] `modules/sandbox_core/command_bus.{h,cpp}` (`PermissionLevel` tiers, `Variant::Type` signature validation, rate-limiting, 512-byte cap, `#ifdef DEBUG_ENABLED` `DEV` guard, audit log)
- [ ] `modules/sandbox_core/sandbox_serializer.{h,cpp}` (Schema v2 `.sbsav` binary & `.json` saves, `allow_objects = false` enforcement, pre-save hydration flush hook)
- [ ] `modules/sandbox_core/sandbox_debug_server.{h,cpp}` (`#ifdef DEBUG_ENABLED` `127.0.0.1` `TCPServer` with HTTP upgrade `Host`/`Origin`/Bearer token check and `403 Forbidden` rejection)
- [ ] Tier 1 Patch: `main/main.cpp` (`SandboxClock::advance`, `OriginShifter` physics-start hook, main-thread `EventBus::flush_deferred`)

### Phase 2: `modules/sandbox_sim` (`SimulationServer`, `SimModule`, & Rewind Ring Buffer)
- [ ] `modules/sandbox_sim/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_sim/sim_module.{h,cpp}` (`SimModule` resource contract, `on_init`, `on_tick`, `on_snapshot_write`, `on_snapshot_read`, `get_declared_schema`)
- [ ] `modules/sandbox_sim/simulation_server.{h,cpp}` (`RID_Alloc` with `THREAD_SAFE`, priority-bucketed `WorkerThreadPool` dispatch, strict task completion, zero nested waits guard)
- [ ] Built-in Agent SoA Table (`pos64`, `vel`, `rot`, `archetype_id`, `state_flags`, `health`, `hydration_tier`, `pin_mask`, `custom_blob`)
- [ ] Deterministic Rewind Ring Buffer (`sim.rewind <ticks>`, `.sbsav` ring snapshots, `world_state_restored` event)

### Phase 3: `modules/sandbox_narrative` & Tier 1 `AnimationPlayer` Custom Tracks
- [ ] `modules/sandbox_narrative/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_narrative/sandbox_actor.{h,cpp}` & `actor_registry.{h,cpp}` (`SandboxActor`, `ActorRegistry`, `control_lock_requested`, `teleport_requested`, `action_requested`)
- [ ] `modules/sandbox_narrative/sandbox_condition.{h,cpp}` (`SandboxCondition`, `CondStateEquals`, `CondStateRange`, `CondEventFired`, `CondAll`, `CondAny`, `CondNot`)
- [ ] `modules/sandbox_narrative/sequence_action.{h,cpp}`, `sequence_data.{h,cpp}`, & `sequence_director.{h,cpp}` (`SequenceData` DAG, `SequenceDirector`, `Tween::tween_await()`, `ActionPlayAnimation`, `ActionSetState`, `ActionEmitEvent`, `ActionRunCommand`, `ActionLockActor`, `ActionBranchCondition`, `ActionWaitEvent`, `ActionBlendCamera`)
- [ ] `modules/sandbox_narrative/mission_data.{h,cpp}` & `mission_runner.{h,cpp}` (`MissionData`, `ObjectiveData`, `MissionTransition`, `MissionRunner`, `sandbox:narrative_trace` emission)
- [ ] Tier 1 Patch: `scene/resources/animation.{h,cpp}` (`TYPE_EVENT_BUS`, `TYPE_WORLD_STATE`, `TYPE_ACTOR_COMMAND` track types, serialization, `get_cache_type()`)
- [ ] Tier 1 Patch: `scene/animation/animation_mixer.{h,cpp}` (`TrackCacheEventBus`, `TrackCacheWorldState`, `TrackCacheActorCommand`, node-resolution bypass in `_update_caches()`)
- [ ] Tier 1 Patch: `editor/animation/animation_track_editor.cpp` (`type_icons[12]`, `add_track` menu items, keyframe insertion, schema hint strings on `AnimationTrackKeyEdit`)

### Phase 4: `modules/sandbox_streaming` (Opt-In Spatial Partitioning, Per-LOD Async Loader, HLOD, & `OriginShifter`)
- [ ] `modules/sandbox_streaming/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_streaming/world_partition_manifest.{h,cpp}` (Macro/Meso/Micro grid layers, per-LOD path bundles, strict `res://` path-traversal validation)
- [ ] `modules/sandbox_streaming/streaming_invoker.{h,cpp}` (`StreamingInvoker` node with velocity-predictive lookahead $\Delta t_{\text{lookahead}}$)
- [ ] `modules/sandbox_streaming/streaming_server.{h,cpp}` (5-state residency machine, `ResourceLoader::load_threaded_request` priority queue, $\le 2.0\text{ ms}$ time-sliced activator with Jolt colliders first, dithered `hlod_fade` swapper, true RAM/VRAM eviction, sparse cell diffs, `stream.*` commands)
- [ ] `modules/sandbox_streaming/origin_shifter.{h,cpp}` (64-bit `WorldPos64`, grid-snapped atomic physics-start shift, `Node3D::reset_physics_interpolation()`, `RS::multimesh_instances_reset_physics_interpolation()`, `REAL_T_IS_DOUBLE` pass-through)

### Phase 5: `modules/sandbox_bubble` (Opt-In Simulation-to-Node Actor Hydration Bubble)
- [ ] `modules/sandbox_bubble/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_bubble/hydratable_actor.{h,cpp}` (`HydratableActor` component, `hydrated` / `dehydrating` signals, `HYDRATION_PIN` interface)
- [ ] `modules/sandbox_bubble/actor_node_pool.{h,cpp}` (Pre-warmed `Node3D` pools per `archetype_id`, zero steady-state `instantiate()`/`queue_free()`, overflow policies)
- [ ] `modules/sandbox_bubble/hydration_bubble.{h,cpp}` (Tier 0 SoA $\leftrightarrow$ Tier 1 `RS::multimesh_set_buffer()` proxy ring $\leftrightarrow$ Tier 2 pooled actor, $R_{\text{in}}/R_{\text{out}}$ hysteresis, velocity-biased ellipsoid, $\le 1.5\text{ ms}$ frame budget, `flush_active_actors_to_soa()`, `resync_active_actors_from_soa()`, `bubble.*` commands)

### Phase 6: `modules/sandbox_terminal` & Tier 1 `Viewport` Native Terminal Focus
- [ ] Tier 1 Patch: `scene/main/viewport.{h,cpp}` (`set_terminal_focus_viewport`, `clear_terminal_focus_viewport`, direct keyboard/scroll routing in `Viewport::push_input()`, `ui_cancel` release)
- [ ] `modules/sandbox_terminal/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_terminal/terminal_parser.{h,cpp}` (Command tokenizer, typed argument parser, history ring buffer, autocomplete)
- [ ] `modules/sandbox_terminal/terminal_display_3d.{h,cpp}` (`SubViewport` with `disable_3d = true`, min $256 \times 256$ FSR2 guard, MSDF font CLI, sRGB `source_color` CRT shader, `Area3D` 3D-to-2D UV synthesis, captured-mouse camera-center raycast, `SandboxActor` lock, permission & namespace filtering)

### Phase 7: `modules/sandbox_editor` (5-Tab `EditorDock`, `GraphEdit` Canvas, Schema ID Pickers, HLOD Baker, & Export Plugin)
- [ ] `modules/sandbox_editor/config.py`, `SCsub`, `register_types.{h,cpp}`
- [ ] `modules/sandbox_editor/sandbox_editor_dock.{h,cpp}` (5-tab `EditorDock`: Asset Browser, Narrative Flow Graph, Live WorldState & Streaming Inspector, HLOD Baker Utility, Command Console)
- [ ] `modules/sandbox_editor/sandbox_narrative_graph_editor.{h,cpp}` (`GraphEdit` + typed ports `0`/`1`/`2`, `MissionEntryNode`, `ObjectiveNode`, `ConditionNode`, `CombinatorNode`, `SequenceActionNode`, `WaitEventNode`, `BranchNode`, `CommentFrameNode`, `EditorUndoRedoManager` parity, Auto-Arrange DAG, live `sandbox:narrative_trace` highlighting)
- [ ] `modules/sandbox_editor/sandbox_inspector_plugin.{h,cpp}` (`SandboxInspectorPlugin` & `SandboxIdPickerProperty` fuzzy searchable dropdown, namespace tree, validation badges, 3D scene eyedropper & viewport focus buttons)
- [ ] `modules/sandbox_editor/sandbox_hlod_baker.{h,cpp}` (**Option A**: CPU `SurfaceTool` / `ArrayMesh` static cell mesh merger + `Image` rect-packed albedo/normal/ORM texture atlas generator)
- [ ] `modules/sandbox_editor/sandbox_export_plugin.{h,cpp}` (`EditorExportPlugin` duplicating & stripping `_editor_graph_meta` on release export + strict-mode pre-export schema audit)
- [ ] `modules/sandbox_editor/sandbox_editor_debugger.{h,cpp}` & `sandbox_editor_plugin.{h,cpp}` (`EditorDebuggerPlugin` <-> `EngineDebugger` bridge for `sandbox:cmd`, `sandbox:telemetry`, `sandbox:narrative_trace`)

### Phase 8: Verification Gates & Domain Test Harnesses
- [ ] `modules/sandbox_core/tests/test_sandbox_clock.h` & `test_sandbox_security.h`
- [ ] `modules/sandbox_sim/tests/test_simulation_server.h`
- [ ] `modules/sandbox_narrative/tests/test_domain_agnosticism.h`
- [ ] `modules/sandbox_streaming/tests/test_streaming_server.h`
- [ ] `modules/sandbox_bubble/tests/test_hydration_bubble.h`
- [ ] `modules/sandbox_terminal/tests/test_terminal_display_3d.h`
- [ ] `modules/sandbox_editor/tests/test_sandbox_editor.h`

---

## 3. Verification & Acceptance Gates (`SPEC.md` §6)

| Gate | Name | Test Suite / Harness | Status | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Gate 1** | Multi-Clock Isolation Test | `modules/sandbox_core/tests/test_sandbox_clock.h` | `[ ] Pending` | `sim_clock` at $50\times$ and $0\times$; Jolt 60 Hz, audio $1.0\times$, terminal $< 16.6\text{ ms}$ |
| **Gate 2** | Thread Safety & Leak Audit | `modules/sandbox_sim/tests/test_simulation_server.h` | `[ ] Pending` | $10,000$ ticks across $8$ concurrent `SimModule`s on `WorkerThreadPool` |
| **Gate 3** | Domain Agnosticism Proof | `modules/sandbox_narrative/tests/test_domain_agnosticism.h` | `[ ] Pending` | Identical mission, cutscene & terminal on 1st-person and RTS rigs |
| **Gate 4** | 3D Terminal Legibility & Performance | `modules/sandbox_terminal/tests/test_terminal_display_3d.h` | `[ ] Pending` | $4$ simultaneous monitors, MSDF, `disable_3d`, captured mouse, `ui_cancel` |
| **Gate 5** | Security Verification | `modules/sandbox_core/tests/test_sandbox_security.h` | `[ ] Pending` | Save RCE rejection, manifest path traversal rejection, WS `403`, release strip |
| **Gate 6** | High-Speed Open-World Streaming & Origin | `modules/sandbox_streaming/tests/test_streaming_server.h` | `[ ] Pending` | $16\text{ km} \times 16\text{ km}$ at $90\text{ m/s}$, $\ge 4$ origin shifts, $\le 2.0\text{ ms/frame}$ activation, VRAM unload |
| **Gate 7** | Hydration Bubble SoA-to-Node Stress | `modules/sandbox_bubble/tests/test_hydration_bubble.h` | `[ ] Pending` | $2,000$ SoA agents, $100\%$ pool reuse, $500$ round-trip cycles, pre-save flush |
| **Gate 8** | Visual Authoring & Schema Validation | `modules/sandbox_editor/tests/test_sandbox_editor.h` | `[ ] Pending` | $15$-node `GraphEdit` mission, undo/redo, `_editor_graph_meta` strip, schema pickers & 3D eyedropper |

---

## 4. Milestone & Change Log

- **2026-11-22:** Initial Draft v1.0 specification ([SPEC.md](./SPEC.md)) and 6-phase implementation scope ([SCOPE.md](./SCOPE.md)) committed to fork (`KNGKAI/godot`).
- **2026-11-23:** Upgraded [SPEC.md](./SPEC.md) and [SCOPE.md](./SCOPE.md) to Draft v1.2 (added `modules/sandbox_streaming`, `modules/sandbox_bubble`, `SandboxSchemaRegistry`, `GraphEdit` Visual Narrative Editor, `SandboxIdPickerProperty`, and Gates 6–8). Locked in Option A (Combined 3D Proxy Mesh + Texture Atlas) for the HLOD baker and created [STATUS.md](./STATUS.md).
