# Modular 3D Sandbox Framework — Implementation Status

| Attribute | Value |
| :--- | :--- |
| **Specification Baseline** | [SPEC.md](./SPEC.md) (Draft v1.2 — Visual Narrative Authoring, Schema Pickers, Open-World Streaming & Hydration Bubble) |
| **Scope & Constraints** | [SCOPE.md](./SCOPE.md) (8-Phase Work Breakdown & Codebase Audit Findings 1–10) |
| **Current Phase** | **Phase 8 complete** |
| **Overall Progress** | `8 / 8` implementation phases complete. All eight acceptance gates passed on the recorded builds and workloads. |

---

## 1. Phase Summary

| Phase | Subsystem / Target | Tier | Status | Verification Gate |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 1** | `modules/sandbox_core` & `main/main.cpp` (`SandboxClock`) | Tier 1 & 2 | `[x] Complete` | Gate 1, Gate 5 |
| **Phase 2** | `modules/sandbox_sim` (`SimulationServer`, `SimModule`, Rewind) | Tier 2 | `[x] Complete` | Gate 2 |
| **Phase 3** | `modules/sandbox_narrative` & `Animation` Custom Tracks | Tier 1 & 2 | `[x] Complete` | Gate 3 |
| **Phase 4** | `modules/sandbox_streaming` (`WorldPartitionGrid`, HLOD, `OriginShifter`) | Tier 2 (Opt-In) | `[x] Complete` | Gate 5, Gate 6 |
| **Phase 5** | `modules/sandbox_bubble` (`HydrationBubble`, `ActorNodePool`, `MultiMesh`) | Tier 2 (Opt-In) | `[x] Complete` | Gate 7 |
| **Phase 6** | `modules/sandbox_terminal` & `Viewport` Native Terminal Focus | Tier 1 & 3 | `[x] Complete` | Gate 4 |
| **Phase 7** | `modules/sandbox_editor` (`EditorDock`, `GraphEdit`, Schema Pickers, HLOD Baker) | Tier 3 | `[x] Complete` | Gate 8 |
| **Phase 8** | End-to-End Verification Gates & Domain Test Harnesses | Tier 4 | `[x] Complete` | Gates 1–8 |

---

## 2. Detailed Implementation Checklist

### Phase 1: `modules/sandbox_core` & Tier 1 `SandboxClock` Main-Loop Hook
- [x] `modules/sandbox_core/config.py`, `SCsub`, `register_types.{h,cpp}`
- [x] `modules/sandbox_core/sandbox_clock.{h,cpp}` (`sim_clock`, `physics_clock`, `narrative_ui_clock`, spiral-of-death frame budget cap)
- [x] `modules/sandbox_core/world_state.{h,cpp}` (Named Scalar SoA Channel Bank + `RID` Entity SoA Tables with structural lock/commit)
- [x] `modules/sandbox_core/sandbox_schema_registry.{h,cpp}` (`SandboxSchemaRegistry` `.tres` manifest, dynamic discovery merge, type/deprecation validator)
- [x] `modules/sandbox_core/event_bus.{h,cpp}` (Pub-sub router, thread-aware `emit()` -> mutex-guarded `dispatch_deferred` ring buffer, main-thread flush, history log)
- [x] `modules/sandbox_core/command_bus.{h,cpp}` (`PermissionLevel` tiers, `Variant::Type` signature validation, rate-limiting, 512-byte cap, `#ifdef DEBUG_ENABLED` `DEV` guard, audit log)
- [x] `modules/sandbox_core/sandbox_serializer.{h,cpp}` (Schema v2 `.sbsav` binary & `.json` saves, `allow_objects = false` enforcement, pre-save hydration flush hook)
- [x] `modules/sandbox_core/sandbox_debug_server.{h,cpp}` (`#ifdef DEBUG_ENABLED` `127.0.0.1` `TCPServer` with HTTP upgrade `Host`/`Origin`/Bearer token check and `403 Forbidden` rejection)
- [x] Tier 1 Patch: `main/main.cpp` (`SandboxClock::advance`, `OriginShifter` physics-start hook, main-thread `EventBus::flush_deferred`)
- [x] Doctest Suites: `modules/sandbox_core/tests/test_sandbox_clock.h` (Gate 1) & `modules/sandbox_core/tests/test_sandbox_security.h` (Gate 5)

### Phase 2: `modules/sandbox_sim` (`SimulationServer`, `SimModule`, & Rewind Ring Buffer)
- [x] `modules/sandbox_sim/config.py`, `SCsub`, `register_types.{h,cpp}`
- [x] `modules/sandbox_sim/sim_module.{h,cpp}` (`SimModule` resource contract, `on_init`, `on_tick`, `on_snapshot_write`, `on_snapshot_read`, `get_declared_schema`)
- [x] `modules/sandbox_sim/simulation_server.{h,cpp}` (`RID_Owner<..., true>` with `THREAD_SAFE`, priority-bucketed `WorkerThreadPool` dispatch, strict task completion, zero nested waits guard)
- [x] Built-in Agent SoA Table (`pos64`, `vel`, `rot`, `archetype_id`, `state_flags`, `health`, `hydration_tier`, `pin_mask`, `custom_blob`)
- [x] Deterministic Rewind Ring Buffer (`sim.rewind <ticks>`, `.sbsav` ring snapshots, `world_state_restored` event)
- [x] Doctest Suite: `modules/sandbox_sim/tests/test_simulation_server.h` (Gate 2)

### Phase 3: `modules/sandbox_narrative` & Tier 1 `AnimationPlayer` Custom Tracks
- [x] `modules/sandbox_narrative/config.py`, `SCsub`, `register_types.{h,cpp}`
- [x] `modules/sandbox_narrative/sandbox_actor.{h,cpp}` & `actor_registry.{h,cpp}` (`SandboxActor`, `ActorRegistry`, `control_lock_requested`, `teleport_requested`, `action_requested`)
- [x] `modules/sandbox_narrative/sandbox_condition.{h,cpp}` (`SandboxCondition`, `CondStateEquals`, `CondStateRange`, `CondEventFired`, `CondAll`, `CondAny`, `CondNot`)
- [x] `modules/sandbox_narrative/sequence_action.{h,cpp}`, `sequence_data.{h,cpp}`, & `sequence_director.{h,cpp}` (`SequenceData` DAG, `SequenceDirector`, `Tween` interval/await integration, `ActionPlayAnimation`, `ActionSetState`, `ActionEmitEvent`, `ActionRunCommand`, `ActionLockActor`, `ActionBranchCondition`, `ActionWaitEvent`, `ActionBlendCamera`)
- [x] `modules/sandbox_narrative/mission_data.{h,cpp}` & `mission_runner.{h,cpp}` (`MissionData`, `ObjectiveData`, `MissionTransition`, `MissionRunner`, `sandbox:narrative_trace` emission)
- [x] Tier 1 Patch: `scene/resources/animation.{h,cpp}` (`TYPE_EVENT_BUS`, `TYPE_WORLD_STATE`, `TYPE_ACTOR_COMMAND` track types, serialization, `get_cache_type()`)
- [x] Tier 1 Patch: `scene/animation/animation_mixer.{h,cpp}` (`TrackCacheEventBus`, `TrackCacheWorldState`, `TrackCacheActorCommand`, node-resolution bypass in `_update_caches()`)
- [x] Tier 1 Patch: `editor/animation/animation_track_editor.cpp` (`type_icons[12]`, `add_track` menu items, keyframe insertion, schema hint strings on `AnimationTrackKeyEdit`)
- [x] Doctest Suite: `modules/sandbox_narrative/tests/test_domain_agnosticism.h` (Gate 3)

### Phase 4: `modules/sandbox_streaming` (Opt-In Spatial Partitioning, Per-LOD Async Loader, HLOD, & `OriginShifter`)
- [x] `modules/sandbox_streaming/config.py`, `SCsub`, `register_types.{h,cpp}`
- [x] `modules/sandbox_streaming/world_partition_manifest.{h,cpp}` (Macro/Meso/Micro grid layers, per-LOD path bundles, strict `res://` path-traversal validation)
- [x] `modules/sandbox_streaming/streaming_invoker.{h,cpp}` (`StreamingInvoker` node with velocity-predictive lookahead $\Delta t_{\text{lookahead}}$)
- [x] `modules/sandbox_streaming/streaming_server.{h,cpp}` and `cell_residency.{h,cpp}` implement priority-ordered loading, five residency states, collision-first activation, HLOD transitions, resource eviction, sparse diff delivery, and the five `stream.*` commands. Within the default 2 ms budget the activator admits one scene instantiation or attachment early in the frame and defers the next. Each authored subtree is indivisible. Compatible materials consume the `hlod_fade` and `hlod_fade_outgoing` instance uniforms.
- [x] `modules/sandbox_streaming/origin_shifter.{h,cpp}` (64-bit `WorldPos64`, grid-snapped atomic physics-start shift, `Node3D::reset_physics_interpolation()`, `RS::multimesh_instances_reset_physics_interpolation()`, `REAL_T_IS_DOUBLE` pass-through)

### Phase 5: `modules/sandbox_bubble` (Opt-In Simulation-to-Node Actor Hydration Bubble)

Hosts register their packed actor scenes and pool capacities, then call `HydrationBubble.start(world_root)`. Clock and save/load callbacks run while that world is active.

- [x] `modules/sandbox_bubble/config.py`, `SCsub`, `register_types.{h,cpp}`
- [x] `modules/sandbox_bubble/hydratable_actor.{h,cpp}` (`HydratableActor` component, `hydrated` / `dehydrating` signals, `HYDRATION_PIN` interface)
- [x] `modules/sandbox_bubble/actor_node_pool.{h,cpp}` (Pre-warmed `Node3D` pools per `archetype_id`, zero steady-state `instantiate()`/`queue_free()`, overflow policies)
- [x] `modules/sandbox_bubble/hydration_bubble.{h,cpp}` (Tier 0 SoA $\leftrightarrow$ Tier 1 `RS::multimesh_set_buffer()` proxy ring $\leftrightarrow$ Tier 2 pooled actor, $R_{\text{in}}/R_{\text{out}}$ hysteresis, velocity-biased ellipsoid, $\le 1.5\text{ ms}$ frame budget, `flush_active_actors_to_soa()`, `resync_active_actors_from_soa()`, `bubble.*` commands)

### Phase 6: `modules/sandbox_terminal` & Tier 1 `Viewport` Native Terminal Focus

The in-world console routes typed commands through `CommandBus` using its own permission tier, namespace filter, history and caller identity. The host can assign a `SandboxActor` through `actor_path` or pass one to `engage()`. The four-monitor Gate 4 room verified native focus, rendering and input on Windows.

- [x] Tier 1 Patch: `scene/main/viewport.{h,cpp}` (`set_terminal_focus_viewport`, `clear_terminal_focus_viewport`, direct keyboard/scroll routing in `Viewport::push_input()`, `ui_cancel` release)
- [x] `modules/sandbox_terminal/config.py`, `SCsub`, `register_types.{h,cpp}`
- [x] `modules/sandbox_terminal/terminal_parser.{h,cpp}` (Command tokenizer, typed argument parser, history ring buffer, autocomplete)
- [x] `modules/sandbox_terminal/terminal_display_3d.{h,cpp}` (`SubViewport` with `disable_3d = true`, min $256 \times 256$ FSR2 guard, MSDF font CLI, sRGB `source_color` CRT shader, `Area3D` 3D-to-2D UV synthesis, captured-mouse camera-center raycast, `SandboxActor` lock, permission & namespace filtering)

### Phase 7: `modules/sandbox_editor` (5-Tab `EditorDock`, `GraphEdit` Canvas, Schema ID Pickers, HLOD Baker, & Export Plugin)
- [x] `modules/sandbox_editor/config.py`, `SCsub`, `register_types.{h,cpp}` (editor target only)
- [x] `modules/sandbox_editor/sandbox_editor_dock.{h,cpp}` (5-tab `EditorDock`: Asset Browser, Narrative Flow Graph, Live WorldState & Streaming Inspector, HLOD Baker Utility, Command Console)
- [x] `modules/sandbox_editor/sandbox_narrative_graph_editor.{h,cpp}` (`GraphEdit` + typed ports `0`/`1`/`2`, `MissionEntryNode`, `ObjectiveNode`, `ConditionNode`, `CombinatorNode`, `SequenceActionNode`, `WaitEventNode`, `BranchNode`, `CommentFrameNode`, `EditorUndoRedoManager` parity, Auto-Arrange DAG, live `sandbox:narrative_trace` highlighting)
- [x] `modules/sandbox_editor/sandbox_inspector_plugin.{h,cpp}` (`SandboxInspectorPlugin` & `SandboxIdPickerProperty` fuzzy searchable dropdown, namespace tree, validation badges, 3D scene eyedropper & viewport focus buttons)
- [x] `modules/sandbox_editor/sandbox_hlod_baker.{h,cpp}` (**Option A**: CPU `SurfaceTool` / `ArrayMesh` static cell mesh merger + `Image` rect-packed albedo/normal/ORM texture atlas generator)
- [x] `modules/sandbox_editor/sandbox_export_plugin.{h,cpp}` (`EditorExportPlugin` duplicating & stripping `_editor_graph_meta` on release export + strict-mode pre-export schema audit)
- [x] `modules/sandbox_editor/sandbox_editor_debugger.{h,cpp}` & `sandbox_editor_plugin.{h,cpp}` (`EditorDebuggerPlugin` <-> `EngineDebugger` bridge for `sandbox:cmd`, `sandbox:telemetry`, `sandbox:narrative_trace`)

### Phase 8: Verification Gates & Domain Test Harnesses
- [x] `modules/sandbox_core/tests/test_sandbox_clock.h` & `test_sandbox_security.h`
- [x] `modules/sandbox_sim/tests/test_simulation_server.h`
- [x] `modules/sandbox_narrative/tests/test_domain_agnosticism.h`
- [x] `modules/sandbox_streaming/tests/` — manifest validation, cell residency and HLOD, server integration, and floating-origin traversal doctests
- [x] `modules/sandbox_bubble/tests/test_hydration_bubble.h`
- [x] `modules/sandbox_terminal/tests/test_terminal_display_3d.h`, `test_terminal_focus.h` and `test_terminal_parser.h`
- [x] `modules/sandbox_editor/tests/` and the `narrative_editor`, `editor_picker` and `editor_bridge` integration projects.
- [x] `tests/sandbox_harnesses/verify_suite.py` checks the doctest XML totals, asserts that tests ran, and requires executed cases for all eight gates.
- [x] The Windows clock project and two gameplay projects have recorded Gate 1 and Gate 3 runtime results.
- [x] The full worker stress case passed independently under GCC ThreadSanitizer and AddressSanitizer with leak detection.

---

## 3. Verification & Acceptance Gates (`SPEC.md` §6)

| Gate | Name | Test Suite / Harness | Status | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Gate 1** | Multi-Clock Isolation Test | [Windows clock project](tests/sandbox_harnesses/clock/README.md) and clock doctest | Passed | Jolt 60.037/60.039 Hz and normal WASAPI playback at 50× and paused. All 104 terminal inputs reached post-draw in under 16.6 ms. |
| **Gate 2** | Thread Safety & Leak Audit | [Linux sanitizer runner](tests/sandbox_harnesses/threading/README.md) and simulation doctest | Passed | Windows, TSan and ASan with leak detection each passed 65 assertions. All 80,000 tasks were awaited. |
| **Gate 3** | Domain Agnosticism Proof | [First-person and RTS projects](tests/sandbox_harnesses/domains/README.md) and narrative doctests | Passed | The same five shared files produced the expected behavior in both hosts. All three Gate 3 cases passed with 58 assertions. |
| **Gate 4** | 3D Terminal Legibility & Performance | `modules/sandbox_terminal/tests/test_terminal_display_3d.h` | Passed | Four simultaneous monitors, 64 recognized title samples, renderer pass counts, captured interaction and focus release. [Hardware evidence](tests/sandbox_harnesses/terminal/results/result.json). |
| **Gate 5** | Security Verification | Core security and streaming manifest tests, Windows release probe | Passed | All 5 Gate 5 doctests passed (79 assertions). A live debug connection returned HTTP 403. The release template rejected the listener and DEV commands. |
| **Gate 6** | High-Speed Open-World Streaming & Origin | [Hardware traversal](tests/sandbox_harnesses/streaming/README.md) and streaming doctests | Passed | $16\text{ km} \times 16\text{ km}$ at $90\text{ m/s}$, $\ge 4$ origin shifts, $\le 2.0\text{ ms/frame}$ activation, VRAM unload |
| **Gate 7** | Hydration Bubble SoA-to-Node Stress | `modules/sandbox_bubble/tests/test_hydration_bubble.h` | Passed | $2,000$ SoA agents, $100\%$ pool reuse, $500$ round-trip cycles, pre-save flush |
| **Gate 8** | Visual Authoring & Schema Validation | Native editor probes and `modules/sandbox_editor/tests/` | Passed | 55 authoring checks, 34 picker checks, 15 live debugger checks, release package readback and strict rejection. |

---

## 4. Milestone & Change Log

- **2026-11-22:** Initial Draft v1.0 specification ([SPEC.md](./SPEC.md)) and 6-phase implementation scope ([SCOPE.md](./SCOPE.md)) committed to fork (`KNGKAI/godot`).
- **2026-11-23:** Upgraded [SPEC.md](./SPEC.md) and [SCOPE.md](./SCOPE.md) to Draft v1.2 (added `modules/sandbox_streaming`, `modules/sandbox_bubble`, `SandboxSchemaRegistry`, `GraphEdit` Visual Narrative Editor, `SandboxIdPickerProperty`, and Gates 6–8). Locked in Option A (Combined 3D Proxy Mesh + Texture Atlas) for the HLOD baker and created [STATUS.md](./STATUS.md).
- **2026-11-23:** Completed **Phase 1** (`modules/sandbox_core` & Tier 1 `main/main.cpp` `SandboxClock` + `EventBus` hooks), including `SandboxClock`, `WorldState` dual SoA storage, `SandboxSchemaRegistry`, `EventBus`, `CommandBus`, `SandboxSerializer` (Schema v2), `SandboxDebugServer`, and Gate 1 / Gate 5 doctest suites (`test_sandbox_clock.h`, `test_sandbox_security.h`). Verified clean compilation via SCons (`libmodule_sandbox_core`, `libmodules`, `main.o`, `test_main.o`).
- **2026-11-23:** Completed **Phase 2** (`modules/sandbox_sim`), including `SimModule` (`GDVIRTUAL` + C++ virtual contract), `SimulationServer` (`RID_Owner<..., true>`, priority-bucketed `WorkerThreadPool` dispatch enforcing Concurrency Invariants #1–#4, built-in contiguous Agent SoA table, and deterministic `.sbsav` rewind ring buffer with `sim.rewind <ticks>` & `sim.stats`), and Gate 2 doctest suite (`test_simulation_server.h`). Verified clean compilation via SCons (`libmodule_sandbox_sim`, `libmodules`, `test_main.o`).
- **2026-11-23:** Completed **Phase 3** (`modules/sandbox_narrative` & Tier 1 `AnimationPlayer` Custom Tracks), including `SandboxActor` & `ActorRegistry`, `SandboxCondition` & 6 built-in condition combinators, `SequenceAction` (8 built-in action classes), `SequenceData` & `SequenceDirector` DAG runner, `MissionData` (`ObjectiveData`, `MissionTransition`) & `MissionRunner` (`sandbox:narrative_trace` debugger streaming, `SandboxSerializer` hooks, `mission.*` commands), Tier 1 patches to `animation.{h,cpp}`, `animation_mixer.{h,cpp}`, and `animation_track_editor.cpp` (`TYPE_EVENT_BUS`, `TYPE_WORLD_STATE`, `TYPE_ACTOR_COMMAND`), and Gate 3 doctest suite (`test_domain_agnosticism.h`). Verified clean compilation via SCons (`libmodule_sandbox_narrative`, `animation.o`, `animation_mixer.o`, `animation_track_editor.o`, `test_main.o`).

- **2026-11-23:** Started Phase 4. Added runtime opt-in streaming, partition descriptors, predictive invokers, per-LOD resource loading, and the floating origin using the existing clock callbacks. The Windows MSVC tests-enabled configuration compiled the streaming library and test translation unit with `accesskit=no` and `d3d12=no` (both optional SDKs are absent). Full editor, runtime tests and renderer measurements pending.

- **2026-11-23** Completed Phase 4 after the three regression cases, all ten streaming cases, the full engine suite, the Gate 5 release probe and the Gate 6 hardware checks passed. See the measurements below.

- **2026-11-23** Completed Phase 5 and Gate 7 on Windows. The opt-in actor hydration bubble passed the moving-agent district, state round trips, snapshot restoration and the full engine suite.

- **2026-11-23** Completed Phase 6 and Gate 4. The Windows editor passed the terminal cases, full headless suite and four-monitor hardware test.

- **2026-11-23** Completed Phase 7 and Gate 8. The Windows editor passed the graph, schema, debugger, HLOD and release-export checks and the full headless suite.

- **2026-11-23** Completed Phase 8. The rendered multi-clock test, unsuppressed Linux sanitizer runs, and first-person and RTS editor and release games closed Gates 1–3. The Windows full suite passed 1,496 cases. All eight gates have recorded acceptance evidence.
- **2026-11-24** Implemented all 8 UX/UI polish improvements across `modules/sandbox_editor`: (1) toggle state & armed tooltip feedback for the 3D Eyedropper, (2) auto-selection & Up/Down/Enter keyboard navigation in `SandboxIdPickerProperty`, (3) `focus_exited` title commit on `GraphFrame`, (4) Undo/Redo layout persistence for `Auto-Arrange (Layered DAG)`, (5) right-click canvas node palette (`popup_request`) and empty-state prompt on `SandboxNarrativeGraphEditor`, (6) search filter, type dropdown, and editor theme icons in the `Assets` tab, (7) Up/Down command history & autocomplete navigation in the `Console` tab, and (8) `EditorFileDialog` directory browser button in `SandboxHLODBaker`. Verified with 10 `[SandboxEditor]` doctest cases (172 assertions) and all 104 native Gate 8 editor checks (`55` graph, `34` picker, `15` bridge).

<!-- verification-evidence:start -->
## Phase 4 verification evidence

- **Checked** 2026-11-23 on Windows x86_64 with a tests-enabled MSVC editor and a separate release template.
- **Verified** The clean editor suite, Gate 5 release-template checks and Gate 6 hardware traversal passed.
- **Overall result** Phase 4 accepted on the generated 16 km × 16 km world.

| Check | Evidence | Result |
| --- | --- | --- |
| Module build | MSVC and SCons compiled all six streaming implementation files and produced `module_sandbox_streaming.windows.editor.x86_64.lib`. | Pass |
| Test discovery and compilation | SCons included all four streaming test headers in `modules_tests.gen.h`. The engine test translation unit compiled. A subsequent C++17 syntax check included all ten current cases. | Pass |
| Formatting | The scoped clang-format check and `git diff --check` exited successfully. | Pass |
| Windows editor build | Full tests-enabled MSVC editor build linked in 1h 18m 45s. The final clean relink took 1m 23s with MSVC, platform=windows, target=editor, arch=x86_64, tests=yes, accesskit=no and d3d12=no. [Build log](bin/phase4-signoff/final-clean.log). | Pass |
| Streaming headless execution | With an isolated writable cache, the rebuilt editor ran all 10 streaming tests (221 assertions) successfully. [Doctest summary](bin/phase4-signoff/clean-streaming.log). | Pass |
| Full engine suite | Rebuilt Windows editor, headless with a fresh writable cache. Doctest reported 1,468 passed, 0 failed, 3 skipped, and 533,450 assertions passed. [Doctest summary](bin/phase4-signoff/restored-clean-full.log). | Pass |
| Focused reproduction | The original editor ran all three named cases together. All three failed with 16 failed assertions. Both binary readers reached the Variant decoder at the length prefix. ClassDB reported five singleton-typed parameters and one unnamed parameter. [Original failures](bin/phase4-signoff/baseline.log). | Reproduced |
| Repaired cases | The writer retains its v2 wire format. The reader validates the length framing and also accepts previously readable unframed dictionaries. The simulation callback preserves independently supplied module blobs. Script bindings retain their names, defaults and native dispatch and validate the supplied objects. All three cases passed (155,921 assertions) on the rebuilt editor. A GDScript smoke test also exercised both SimModule callbacks, the condition default arguments and the velocity getter. [Doctest summary](bin/phase4-signoff/clean-repaired.log). | Pass |
| Gate 5 runtime | Debug positive control: an unauthenticated allowlisted-origin loopback request received HTTP 403 and EOF. Windows x86_64 MSVC `target=template_release tests=no accesskit=no d3d12=no`, with all four sandbox modules, GDScript, WebSocket, 3D physics, Vulkan and OpenGL enabled. PID 25968 rejected listener startup with `ERR_UNAUTHORIZED` and reported port 0. Windows observed its ordinary positive-control socket on 52801, then three empty listener samples and connection refusal on the requested port 52797 (10061). Both built-in and custom DEV commands were absent, a DEV caller was denied, and a USER control succeeded. [Probe](tests/sandbox_harnesses/security/verify_release.py), [configuration](bin/gate5-release-check/build-scheduling-candidate.log), [runtime evidence](bin/gate5-release-check/scheduling-candidate-audit/evidence.json). | Pass |
| Gate 6 rendered traversal | 16 km × 16 km, 4,096 cells (250 m sides), 120 textured town blocks and 3 shared rural layouts. GeForce GTX 1050 Ti (4 GiB, driver 582.78), Vulkan 1.4.312 Forward+, 1280 × 720, VSync off, 120 FPS cap, 60 Hz physics interpolation. The final uninstrumented editor traversed 15,550 m at 90 m/s across 7 origin shifts. All 676,676 road/roof samples and 2,982,473,240 road-corridor pixel checks matched across all 15,397 rendered frames, including 4,329 visible fade frames and 28 shift-window frames. [Visual result and captures](tests/sandbox_harnesses/streaming/results/candidate-680b7a89/visual.json). | Pass for sampled rendered continuity |
| Gate 6 performance calibration | During compiler activity, without pixel readbacks, p99 was 15.587 ms and peak activation was 4.201 ms. With 16 rural cells at each end, total Vulkan allocation bytes were 40,090,512 before, 51,860,704 at the dense peak, and 41,139,088 after eviction. Texture and buffer counters returned to baseline and all 360 dense atlases left the resource cache. An independent parallel-upload test reproduced a 1 MiB retained driver allocation. | Cold calibration measured |
| Gate 6 cause isolation | The renderer trace identified the retained 1,048,576 bytes as Vulkan upload worker 1's reusable staging buffer. A drive into the first dense district and back established the warm rural baseline at 41,139,088 bytes with identical content. The streaming trace measured a single SceneTree attachment at 1,609 µs. Starting additional instantiation/attachment work late in a frame caused a 2.769 ms peak despite p99 frame time of 10.059 ms. | Cause reproduced |
| Gate 6 scheduler verification | On the uninstrumented editor, the quiet traversal ran 20,735 frames with 7 shifts. The p99 frame time was 9.528 ms and maximum activation was 1.926 ms. Zero frames exceeded 2.0 ms. [Frame measurements](tests/sandbox_harnesses/streaming/results/candidate-680b7a89/timing-frames.csv). All-heap Vulkan bytes were 40,090,512 cold, 41,139,088 after warm-up, 52,905,248 at the measured peak, and 42,187,664 after eviction. Texture and buffer bytes returned exactly to their entry levels. | Timing passed |
| Gate 6 dedicated VRAM | The same world and GPU were measured with per-heap Vulkan counters. Device-local bytes were 36,940,688 cold and at entry, 49,772,560 at the dense peak, and 36,940,688 after eviction. The non-device-local heaps rose from 3,149,824 cold to 4,198,400 warm and stayed there throughout this memory run. Per-process Windows samples on the NVIDIA adapter independently recorded 213,024,768 dedicated bytes at both settled endpoints. Shared bytes rose by 16,384. The 16 rural cells had identical batch counts and none of the 360 urban atlas paths remained cached. [Joined heap and Windows measurements](tests/sandbox_harnesses/streaming/results/memory-analysis.json). | Pass |
<!-- verification-evidence:end -->

## Phase 5 verification evidence

The Windows x86_64 MSVC editor was built with `target=editor tests=yes accesskit=no d3d12=no`. Its SHA-256 is `28c74b03eeec346c7b47791206ea2005e117c91c971cf314e702c1a50ce7d554`. [Final build log](bin/phase5/final-build.log).

| Check | Final observation |
| --- | --- |
| Focused hydration tests | 8 passed, zero failed, 13,195 assertions. Cases covered both pool-overflow policies, a 2,000-agent traversal, hysteresis, moving invokers, origin shifts, and 500 hydrate/dehydrate cycles with independent pins, health, velocity, damage flags and custom bytes. [Doctest summary](bin/phase5/final-focused.log). |
| Save, rewind and shutdown | Binary and JSON saves flushed live state and restored it into the bound shell. A GDScript controller contributed state via the capture signal. A five-tick rewind restored its position, health, pin and RID to the same shell. Removing the world released the pool and removed its commands. [Rewind probe](tests/sandbox_harnesses/hydration/rewind_probe.gd), [runtime log](bin/phase5/rewind-probe.log). |
| Full headless suite | 1,476 passed, zero failed, three skipped and 546,820 assertions passed. The focused and full runs used separate writable caches. [Doctest summary](bin/phase5/final-full.log). |
| Initial timing finding | The first rendered run recorded one 2.766 ms update. The transition timer excluded classification and proxy submission. The revised scheduler admits transitions against a deadline measured from frame entry, reserves time for proxy upload, and avoids redundant lookups while packing the visible batch. |
| Final headless district | Full bubble update p99 0.667 ms, maximum 0.905 ms, zero updates over 1.5 ms. [720 measured frames](tests/sandbox_harnesses/hydration/results/headless.json). |
| Final rendered district | NVIDIA GeForce GTX 1050 Ti, Vulkan 1.4.312 Forward+, 1280 x 720, VSync off, 60 FPS cap. Full bubble update p99 1.146 ms, maximum 1.322 ms, zero updates over 1.5 ms. The whole frame interval had p99 17.585 ms. [720 measured frames](tests/sandbox_harnesses/hydration/results/windows-forward-plus.json). |

The district simulated 400 vehicles and 1,600 pedestrians. Each run followed a 1,440 m out-and-back route (2,880 m total) for 720 warm-up frames and repeated it for 720 measured frames. Both pools reached capacity (24 traffic and 64 pedestrian shells). The rendered run recorded 2,566 hydrations and 2,565 dehydrations, zero shell constructions or destructions after prewarm, proxies on all 720 measured frames and all 60 worker groups awaited. All recorded frames stayed within the four-hydration and six-dehydration caps. The timing result applies to this workload and these prefabs. [Workload and reproduction](tests/sandbox_harnesses/hydration/README.md).

## Phase 6 verification evidence

The Windows x86_64 MSVC editor was built with `target=editor tests=yes accesskit=no d3d12=no`. The hardware report records the verified executable SHA-256 `bb004e19573879f7166b57ad4768d9fe25f87405c88336317f656b6d4a3d5cfb`. [Editor and scene library build](bin/phase6/base-build.log), [editor link](bin/phase6/editor-build.log).

| Check | Observed result |
| --- | --- |
| Focused headless tests | All 8 terminal cases passed with 296 assertions. They exercised four independent 2D targets, the 256-pixel minimum on both axes, MSDF fonts, shader sampling, actor lock transfer, cancellation, input isolation, parser limits, command permissions and filtered completion. [Doctest summary](bin/phase6/terminal.log). |
| Full headless suite | With a fresh writable cache, doctest reported 1,484 passed, zero failed, three skipped, and all 547,346 assertions passed. [Full-suite log](bin/phase6/full.log). |
| Rendered legibility | GeForce GTX 1050 Ti, Vulkan 1.4.312 Forward+, 1600 x 900, four 768 x 432 terminal targets. Windows English OCR read all 64 sampled titles correctly, covering each integer distance from 1 to 8 m at both 0 and 45 degrees. The blank control returned no text. The result applies to the recorded font, resolution and viewing conditions. [Room capture](tests/sandbox_harnesses/terminal/results/room.png), [original 8 m crop at 45 degrees](tests/sandbox_harnesses/terminal/results/Console0-8m-45deg.png). |
| Occlusion and throttling | All five viewports rendered across eight distinct profile frames per configuration. Each frame had one 3D scene and one occlusion update normally, five of each with four additional active 3D cameras, and one of each after disabling terminal 3D again. A distant terminal and one behind the camera each drew zero times in four frames. The nearby visible terminal drew four times. [Renderer traces](tests/sandbox_harnesses/terminal/results/result.json). |
| Live input and commands | A solid body blocked the captured click. Removing it let the camera-center ray acquire the terminal and lock the actor. Motion reached pixel (479.84, 183.65) against the expected (480, 183.6). The wheel arrived once, Enter submitted once, and Escape released the actor. A GDScript command received a quoted argument and a disallowed namespace returned `ERR_UNAUTHORIZED`. [Harness and reproduction](tests/sandbox_harnesses/terminal/README.md). |

## Phase 7 verification evidence

The Windows x86_64 MSVC editor was built with `target=editor tests=yes accesskit=no d3d12=no`. All final probes used executable SHA-256 `326b75143f94d5d53d78634c4b91e6b2debcbd9ee1035a00bc259c9174ef7628`. [Build log](bin/phase7/final-build.log) and [Gate 8 results](tests/sandbox_harnesses/narrative_editor/results/gate8.json).

| Check | Observed result |
| --- | --- |
| Headless tests | All 10 Phase 7 cases and 156 assertions passed. The full suite passed 1,494 cases and 547,548 assertions, with zero failures and three skipped cases. Both runs used fresh writable caches. [Focused log](bin/phase7/final-focused.log) and [full log](bin/phase7/verified-full.log). |
| Graph authoring | The native editor passed 55 checks. The probe authored a 15-node branching mission, linked a five-step sequence, used the Scene menu to undo and redo edits, and reloaded the saved resource. The suite also verified that an explicit join releases its successor once after both branches finish. [Authoring probe](tests/sandbox_harnesses/narrative_editor/verify.py). |
| Schema and animation | All 34 editor checks passed. Actor and marker clicks updated the host Inspector and an actor-command key, including its existing marker payload. Fuzzy lookup, type and missing-ID badges, sibling payload retention, and 3D focus selection passed. [Picker probe](tests/sandbox_harnesses/editor_picker/verify.py). |
| Debugger and live state | All 15 checks passed in an editor-launched game. The graph showed the awaiting step. A command returned the game PID, autocomplete came from the game, and the visible inspector displayed the same PID, per-module timing, three hydration tiers, VRAM and a resident-cell heatmap. [Debugger probe](tests/sandbox_harnesses/editor_bridge/verify.py). |
| Release export | The Gate 5 Windows release template ran the new export and read back both mission and sequence with empty layout metadata and intact gameplay links. The authored files retained their hashes. Strict export of `fixture.unregistered` reported both resource paths, returned failure, and preserved the previous PCK byte for byte. [Export and readback results](bin/phase7/export-e711f1d0/result.json). |
| HLOD bake | Two disk-based cases passed 43 assertions. The fixture reloaded two partition cells and checked cell-local geometry, a single proxy surface per cell, and albedo, normal and ORM atlas samples. Invalid paths and failure during a later cell left no partial generation. [Baker cases](modules/sandbox_editor/tests/test_sandbox_editor_baker.h). |

Verification exposed two editor defects. The plugin now registers for 3D input when it enters the tree, after the editor has allocated its forwarding list. Graph slot indexing now excludes the internal title bar. The 500-cycle hydration state test also waits for the scheduler to finish each transition within a bounded number of frames and checks the original pin, state and cycle-count assertions.

## Phase 8 verification evidence

The Phase 7 editor established the baseline in a fresh writable profile. The full suite executed 1,494 cases, with zero failures, three skipped cases and 547,548 passing assertions. [Baseline result](bin/phase8/baseline/suite-891a214e5e/result.json).

The Windows x86_64 MSVC editor was rebuilt with `target=editor tests=yes accesskit=no d3d12=no`. The verified executable SHA-256 is `0a0c54b0710e020df10ca05ea948c36dead3240f029c39c0a00bb07b6b6533fa`. [Build log](bin/phase8/verified-editor.log).

| Check | Observed result |
| --- | --- |
| Gate 1 clocks and terminal | On the GTX 1050 Ti with Vulkan Forward+, simulation advanced at 50.060× and then produced zero ticks while paused. Each run recorded 420 Jolt steps of 1/60 s. WASAPI playhead speed was 1.00176× and 0.99722×. For 52 Windows key messages per mode the longest time to edit was 9.405/8.823 ms and to the post-draw acknowledgement was 11.114/10.500 ms. Each rendered command line changed 3,071 pixels. The input measurement ends at the engine's post-draw signal. [Measurements](tests/sandbox_harnesses/clock/results/gate1.json). |
| Full headless suite | 1,496 cases and 547,603 assertions passed, with zero failures and three skipped cases. The isolated run matched all per-case assertion counts to the XML totals and found 31 executed cases tagged across the eight gates. The skips were two DDS saver cases and the deliberately pending test. [Verified totals and case list](tests/sandbox_harnesses/results/windows-suite.json), [doctest XML](bin/phase8/suite-7a4ab30c32/doctest.xml). |
| Gate 2 worker stress test | Windows and two GCC 15.2 WSL2 builds each passed 65 assertions. Eight workers overlapped on the first tick, all 80,000 callbacks ran on pool tasks, all 80,000 waits succeeded, and all 32 deferred milestone events arrived on the main thread. The two sanitizer runs exited 0 with no race, address or leak diagnostic and no suppressions. Both recorded the same eleven source hashes, matching the working tree. [TSan result](tests/sandbox_harnesses/threading/results/tsan/result.json), [ASan and leak result](tests/sandbox_harnesses/threading/results/asan/result.json). |
| Gate 1 defect and cost | Generating uncached MSDF glyphs on a first keystroke took the original terminal above budget, with post-draw maxima of 27.232/25.725 ms. Preparing printable ASCII once when the first terminal creates its shared font moved that work to first attachment, which took 870/694 ms against 168/169 ms before the change. Later terminals reuse the atlas. The clock unit test also uses an isolated clock instance to preserve the live simulation hook. [Baseline](tests/sandbox_harnesses/clock/results/before-glyph-preparation.json) and [procedure](tests/sandbox_harnesses/clock/README.md). |
| Gate 3 gameplay | Both projects ray-picked the terminal and ran the same mission command, lifecycle sequences and three-track animation. The perspective camera reached 50° and the RTS camera reached a 4 m orthographic size. Both hosts completed the objective and debrief, released the actor lock and exited cleanly. The runner verified identical hashes for the authored resources and shared script. [Host results](tests/sandbox_harnesses/domains/results/gate3.json), [3 cases and 58 assertions](bin/phase8/gate3-doctest.log). |
| Gate 3 exported games | The first export used dummy 3D physics with the legacy default project setting, so both terminal rays missed. Both host projects now explicitly select Jolt. The editor and both exported games passed with identical shared files. The exported games ran headless from separate EXE/PCK directories, with zero failed checks or engine errors. The release template reported a warning for the unused dummy 2D backend. [Initial failure](tests/sandbox_harnesses/domains/results/gate3-release-default-physics.json), [verified Windows packages](tests/sandbox_harnesses/domains/results/gate3-release.json). |
| Gate 4 rendered regression | The four-monitor room passed on the rebuilt editor and Windows OCR recognized all 64 title samples. The blank control was empty. The input, camera, viewport and occlusion checks passed. [Runtime and OCR evidence](tests/sandbox_harnesses/results/gate4-phase8.json). |
| Gate 5 shipping configuration | Windows x86_64 MSVC `target=template_release tests=no accesskit=no d3d12=no sdl=no`, with all six runtime sandbox modules, GDScript, WebSocket, Jolt, FreeType, msdfgen, fallback text, Vulkan and OpenGL. The editor module was excluded. SHA-256 `d563551b597072688380b8e5e630b88630a47240f779e8d4cb3d211f10ac0549`. All 8,394 tracked build/source files checked and all 115 sandbox module source/build files matched the working tree byte for byte. [Exact SCons options and final link](bin/phase8/release-final.log). |
| Gate 5 release runtime | PID 28000 returned `ERR_UNAUTHORIZED` on sandbox listener startup, reported port 0 and had zero listening sockets on three OS samples after closing the observed positive control. Windows refused the connection to port 55589. Built-in and custom DEV commands were unavailable, and a USER control command succeeded. [Socket samples and command results](tests/sandbox_harnesses/results/gate5-release.json). |
| Gates 6 and 7 hardware evidence | Phase 8 reran the tagged unit cases. The hardware sign-offs come from the Phase 4 streaming and Phase 5 hydration workloads. The 16 km × 16 km streaming run at 90 m/s recorded seven shifts, a 9.528 ms p99 frame time, 1.926 ms maximum activation and full dedicated VRAM recovery. The rendered 2,000-agent district recorded a 1.322 ms maximum hydration update and no shell allocations after prewarm. [Streaming measurements and render checks](tests/sandbox_harnesses/results/gate6-phase4-hardware.json), [hydration measurements](tests/sandbox_harnesses/hydration/results/windows-forward-plus.json). |
| Gate 8 editor regression | The native editor repeated all 55 graph authoring checks, all 34 picker checks and all 15 live editor/game debugger checks. [Graph authoring](bin/phase8/graph-authoring.log), [schema picker](bin/phase8/picker-fab96067/result.json), [live debugger](bin/phase8/bridge-4ec25cfb/result.json). |
| Gate 8 shipping export | The new release template read back both authored resources from the exported PCK with layout stripped. Source layout was preserved. Strict export rejected the missing command reference and preserved the successful package byte for byte. [Export and readback result](tests/sandbox_harnesses/results/gate8-release.json). |

The Gate 3 baseline exposed unused lifecycle sequence references, EventBus waits without a runtime subscription and mission handlers with argument lists that did not match CommandBus dispatch. MissionRunner now schedules the linked sequences with cancellation on lifecycle changes and accepts the declared command arguments. SequenceDirector subscribes while it is in the scene tree and waits for synchronous dispatch to unwind before declaring a parallel sequence finished.
