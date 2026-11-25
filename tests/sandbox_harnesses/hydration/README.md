# Gate 7 hydration district

The project configures each archetype with a `PackedScene`, a pool capacity and a proxy mesh. Each scene has a `Node3D` root and one `HydratableActor` child. After adding a `StreamingInvoker` to the world, the project calls `HydrationBubble.start(world_root)`. Leaving the tree or calling `stop()` releases the pools. To start another world, configure its archetypes before calling `start()` again.

On `hydrated(entity_rid, initial_state)`, a controller receives velocity, health, state flags, spline cursor and custom bytes. It can maintain the component's `state` dictionary or write its latest values into the dictionary supplied by `capturing_state` during a save or release. The subsequent `dehydrating` signal runs on release. The bubble reads the live root transform. Independent pin reasons are `mission`, `sequence`, `player_occupied`, `combat_lock` and `debug`.

Run this project with a build containing the sandbox bubble, simulation and streaming modules. From the repository root, use the console executable so PowerShell waits for the report and exposes the exit code.

```powershell
$godot = '.\bin\godot.windows.editor.x86_64.console.exe'
$project = 'tests/sandbox_harnesses/hydration'
$headless = Join-Path $PWD 'bin/phase5-signoff/hydration-headless.json'
& $godot --headless --fixed-fps 60 --path $project -- --evidence-path $headless
$LASTEXITCODE

& $godot --headless --path $project --script res://rewind_probe.gd
$LASTEXITCODE

$rendered = Join-Path $PWD 'bin/phase5-signoff/hydration-vulkan.json'
& $godot --path $project --rendering-method forward_plus --rendering-driver vulkan --audio-driver Dummy --max-fps 60 -- --evidence-path $rendered
$LASTEXITCODE
```

The 1.6 km district has 100 blocks, 400 vehicles and 1,600 pedestrians moving in both directions. The traffic pool prewarms 24 box-shaped physics bodies and the pedestrian pool prewarms 64 capsule-shaped bodies. Both use mesh proxies outside the live bubble. The 5 Hz sandbox simulation clock drives SoA integration and the main loop schedules hydration. Live bodies advance in physics ticks. The invoker and camera cover 1,440 m out and back in 720 process frames, moving 4 m per frame.

The first full circuit warms the pools, proxy buffers and renderer. The second follows the same path and records 720 frame samples. At each process callback the harness records the preceding host frame interval from the monotonic clock, the completed scheduler's full update time and its transition time, tier populations, occupancy by archetype and transition counts. The JSON includes mean, nearest-rank p50/p99 and maximum microseconds, plus the number of scheduler and transition samples above 1,500 µs. Headless fixed-FPS mode advances virtual time and reports actual elapsed CPU-side intervals. The hardware run includes rendering and the frame cap in its wall intervals.

Exit code zero requires both pools and proxies to participate, all 2,000 agents to be accounted for, both kinds of transition, the 4-hydration/6-dehydration caps, completed worker groups, forward movement of a traffic witness, and zero shell constructions or destructions between prewarm and the end of measurement. The full hydration update must stay within 1,500 microseconds on all 720 measured frames. The report also records the functional result independently so a timing miss can be diagnosed. The default evidence destination is `user://hydration-evidence.json`.
