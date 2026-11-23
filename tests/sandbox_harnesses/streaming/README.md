# Gate 6 streaming traversal

Generate the world once, then run both passes with a tests-enabled editor that includes `sandbox_streaming`. These PowerShell commands run from the repository root.

```powershell
$project = 'tests/sandbox_harnesses/streaming'
$godot = '.\bin\godot.windows.editor.x86_64.console.exe'
python "$project/generate_world.py"
& $godot --path $project --rendering-method forward_plus --rendering-driver vulkan --audio-driver Dummy --max-fps 120 -- --warm-district
& $godot --path $project --rendering-method forward_plus --rendering-driver vulkan --audio-driver Dummy --max-fps 120 -- --visual
```

The generated 16,000 m square contains 4,096 independently placed 250 m cells. The route follows a highway through 120 urban blocks with collision, buildings, rooftop details and per-cell 512², 256² and 64² facade textures. The rural cells share three layouts. High and middle LODs use scene batches, and low LODs use building proxies. Terrain, road markings and the red urban roof landmarks persist across LODs. The generator uses Python's standard library and writes native Godot resources and DDS textures to `world/`.

The vehicle first approaches the rural start from the interior to establish residency under the 650 m eviction radius. The measured traversal covers 15,550 m from x = 225 to x = 15,775 at 90 m/s on the 60 Hz physics clock. It records shifts at the production threshold of 2,048 m. Allow about 5½ minutes for the warmed timing pass and 3½ minutes for the visual pass. Each pass saves its JSON summary in `results/`. The exit code is 0 for a passing pass, 1 for an unmet threshold and 2 for an invalid run.

## Frame time and memory

The timing pass uses a forward-facing camera at 1280 × 720 with VSync off. It samples monotonic wall time between consecutive post-draw callbacks, computes the nearest-rank 99th percentile, and records the maximum activation time reported by the streaming server. Run this pass while the machine is idle. Timings from the visual pass include synchronous GPU readbacks.

Memory samples cover settled rural entry, peak urban residency, and settled rural exit. The result includes texture, buffer and total video allocation bytes, the active scene batches at both ends, and the count of town atlas resources still cached after eviction. With `--warm-district`, the vehicle records the cold sparse baseline at x = 225, drives at 90 m/s to x = 5,250 through the first 1,250 m of town, then returns to x = 225. After four settled seconds it begins the measured traversal. This warm-up exercises the driver's shared transfer workers and unloads the district. Omitting that argument measures the route directly against the cold entry baseline. Report the cold and warm totals alongside the peak and final total. The timing pass writes each frame's wall interval, activation time and world position to `results/timing-frames.csv`.

## Rendered continuity

The visual pass uses an overhead camera and reads the viewport on every rendered traversal frame. In a 220 m wide corridor it checks each pixel for the magenta background exposed by missing terrain. An off-world region at entry supplies a positive control. It also checks the interiors and exteriors of yellow pavement markings and red urban roofs against their expected projected positions. The result counts visible fades, every shift frame and its three successors, image mismatches and the positions of origin shifts. The checks cover the road corridor and landmarks. Distant scenery outside this region lies beyond the measurement area.

The project saves images from an urban fade and from before, during and after the third shift. On a visual failure it also saves the first mismatch. Generated assets, captures and machine-specific results are ignored by Git.

## Windows GPU memory cross-check

The optional Vulkan diagnostic patch logs VMA allocation bytes on heaps with `VK_MEMORY_HEAP_DEVICE_LOCAL_BIT` and on the remaining heaps. Build a separate executable with the same SCons options as the ordinary editor. The following example uses the options used for this Windows benchmark and restores the ordinary editor afterwards. Run from a checkout containing the streaming changes.

```powershell
$patch = 'tests/sandbox_harnesses/streaming/vulkan_heap_trace.patch'
git apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'Heap diagnostic patch does not apply cleanly.' }
git apply $patch
if ($LASTEXITCODE -ne 0) { throw 'Could not apply heap diagnostic patch.' }
try {
    scons platform=windows target=editor arch=x86_64 tests=yes accesskit=no d3d12=no
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic build failed.' }
    Copy-Item '.\bin\godot.windows.editor.x86_64.exe' '.\bin\godot.gate6-heaps.exe'
} finally {
    git apply --reverse $patch
    if ($LASTEXITCODE -ne 0) { throw 'Could not reverse heap diagnostic patch.' }
}
scons platform=windows target=editor arch=x86_64 tests=yes accesskit=no d3d12=no
```

Use the separate diagnostic executable for the memory run. This writes `results/memory.json` and `results/memory-frames.csv` while preserving the ordinary timing and visual results.

```powershell
$project = (Resolve-Path 'tests/sandbox_harnesses/streaming').Path
New-Item -ItemType Directory -Force "$project\results" | Out-Null
& '.\bin\godot.gate6-heaps.exe' --path $project --rendering-method forward_plus --rendering-driver vulkan --audio-driver Dummy --max-fps 120 --log-file "$project\results\heaps.log" -- --warm-district --memory | Out-Host
```

Use the PID printed by the harness after Vulkan starts to launch the sampler in a second PowerShell window.

```powershell
$results = (Resolve-Path 'tests/sandbox_harnesses/streaming/results').Path
$benchmarkPid = Read-Host 'PID printed by the benchmark'
& '.\tests\sandbox_harnesses\streaming\sample_windows_gpu.ps1' -ProcessId $benchmarkPid -OutputPath "$results\wddm.csv"
```

The sampler records the Windows GPU Process Memory dedicated and shared counters once per second for each adapter until the process exits. Match the adapter LUID to the Vulkan renderer, especially on systems with hybrid graphics. The JSON `settled_windows` lists the cold, warmed and final sample windows in UTC Unix seconds and engine tick microseconds. Compare WDDM values from the ends of these windows and report their observed range if multiple values occur. Use the per-frame CSV to select the urban portion of the run for a peak.

Godot's Vulkan `RENDERING_INFO_VIDEO_MEM_USED` sums allocation bytes across all Vulkan memory heaps. A dedicated GPU can also allocate CPU upload buffers from host memory. Report this all-heap counter, the engine texture and buffer counts, and the independent WDDM dedicated and shared measurements separately. When a diagnostic Vulkan build provides per-heap allocation events, match its monotonic timestamps to the same windows and identify the device-local and host-only changes. Use the ordinary timing run for performance, the visual run for rendered continuity, and the memory run for this allocation breakdown.

After both the diagnostic run and sampler exit, run `analyze_memory.py` with the low DWORD of the adapter LUID. For example, the test machine's NVIDIA WDDM instance contains `luid_0x00000000_0x0000e761`.

```powershell
python tests/sandbox_harnesses/streaming/analyze_memory.py --adapter-luid 0xe761
```

The script joins the timestamped observations from the same process, checks that the per-heap sum equals the engine's reported total at each measured endpoint, and saves per-check results to `results/memory-analysis.json`. It also reports the greatest dedicated and shared values observed by WDDM during the dense sector. Windows samples residency at 1 Hz, while the Vulkan measurements refer to engine allocation bytes.

`gate6_passed` combines the verified world dimensions, route speed and shifts with frame and activation timing from the ordinary executable, rendered observations, and exact recovery of dedicated VRAM. The analyzer exits 0 when every required check passes and exits 1 for incomplete or failed evidence. The raw `passed` field in the timing and memory JSON files includes timing thresholds and aggregate Vulkan allocation growth. Diagnostic logging can affect frame time, and host-only upload allocations can affect that aggregate. Those raw flags and values remain in the analysis alongside the dedicated-memory verdict.
