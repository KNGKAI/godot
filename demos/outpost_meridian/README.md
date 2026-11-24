# Outpost Meridian — Dual-Perspective Colony, 50× Multi-Clock, & Floating-Origin Showcase

**Outpost Meridian** is an interactive planetary terraforming and autonomous mining sandbox built on the Godot 4 Modular 3D Sandbox Framework ([SPEC.md](../../SPEC.md)). It specifically showcases **Domain Agnosticism (`SandboxActor`)**, **$50\times$ Multi-Clock Isolation (`SandboxClock`)**, **Large-World Floating Origin Shifts (`OriginShifter` + `StreamingServer`)**, and **300-Agent `HydrationBubble` Fleets**.

---

## Framework Architecture Showcased

| Capability | Implementation in `demos/outpost_meridian/` |
| :--- | :--- |
| **Dual-Domain Agnosticism (`SandboxActor` Gate 3)** | Seamlessly switch live at runtime between **Domain A: 1st-Person / Rover Commander Rig** ([commander_rig_controller.gd](scripts/commander_rig_controller.gd), `CharacterBody3D` + perspective `Camera3D`) and **Domain B: Orbital RTS Command Rig** ([orbital_rts_controller.gd](scripts/orbital_rts_controller.gd), top-down orthographic `Camera3D`) via `[Tab]` or `view.mode rts|fp`. Both rigs bind to the same diegetic 3D terminals, missions, and 3-track cutscenes. |
| **$50\times$ Multi-Clock Fast-Forward (`SandboxClock` Gate 1)** | Pressing `[F4]` or running `sim.speed 50` accelerates `TerraformingColonySim` ($O_2$ generation, solar battery storage, regolith ore refining) and `MiningFleetSim` up to **$50\times$ simulated speed** while diegetic 3D terminals, camera panning, and Jolt physics remain at smooth real-time refresh rates. |
| **16 km Crater Streaming & Floating Origin (`OriginShifter` Gate 6)** | 12 Macro ($512\text{ m}$) crater outpost cells ([manifest.tres](baked_cells/macro_1/manifest.tres)) with combined 3D HLOD proxy meshes (`*_proxy.res`) and detailed solar-relay scenes (`*.scn`). Driving the high-speed rover across the crater highway crosses multiple $2048\text{ m}$ `OriginShifter` thresholds, snapping the 32-bit scene origin while preserving 64-bit `Origin64` coordinates. |
| **300-Agent Mining Fleet (`HydrationBubble` Gate 7)** | 120 `ore_hauler` vehicles and 180 `survey_drone` skimmers simulated in contiguous SoA memory, rendering as `MultiMesh` GPU proxies in the mid-field (Tier 1) and hydrating into pooled `Node3D` prefabs ([ore_hauler.tscn](scenes/ore_hauler.tscn), [survey_drone.tscn](scenes/survey_drone.tscn)) within $110\text{ m}$ (Tier 2). |
| **Predictive Telemetry Rewind (`sim.rewind`)** | The Rover & Telemetry Console (`[F3]` or `[F5]`) lets you test colony directives at $50\times$ speed and deterministically roll back `WorldState`, `SimModule` state, and hydrated fleet positions via `SimulationServer.rewind()`. |

---

## Running the Project

### 1. Open in the Godot Sandbox Editor
```powershell
.\bin\godot.windows.editor.x86_64.exe --editor --path demos\outpost_meridian
```
* Open [mission_meridian_expansion.tres](resources/mission_meridian_expansion.tres) in the **Sandbox -> Narrative Flow** tab to inspect the 3-phase terraforming mission graph.
* Press **F5** in the editor to watch live node execution highlighting (`green` active, `cyan` completed), `Origin64` telemetry, and the **Live State** cell residency heatmap.

### 2. Play Interactively
```powershell
.\bin\godot.windows.editor.x86_64.exe --path demos\outpost_meridian
```
* **Controls:**
  * `Tab`: Toggle live between **1st-Person Commander** and **Orbital RTS Command** perspectives
  * `W A S D` (+ `Shift` for High-Speed Rover $120\text{ m/s}$ drive in 1st-Person, or fast camera pan in RTS mode)
  * `Mouse Wheel (in RTS mode)`: Zoom orthographic orbital view ($12\text{ m}\text{–}140\text{ m}$)
  * `Right Mouse Button (in 1st-Person mode)`: Look around
  * `Left Mouse Button (Click on 3D Terminal)`: Engage CLI focus (`Esc` to release)
  * `F1` / `F2` / `F3`: Teleport active rig to **Command Deck** (`MarkerCommandDeck`), **Scrubber Bay** (`MarkerScrubberBay`), or **Rover Telemetry Garage** (`MarkerRoverGarage`)
  * `F4`: Toggle **$50\times$ `SandboxClock` Fast-Forward**
  * `F5`: Execute **Deterministic Rewind** (`SimulationServer.rewind(50)`)

* **Terminal Walkthrough:**
  1. At the **Orbital Command Deck (`[F1]`)**, click the terminal and run:
     ```text
     colony.link_telemetry
     view.mode rts
     ```
  2. Activate colony and fleet directives:
     ```text
     fleet.overclock true
     colony.scrubber_boost true
     sim.speed 50
     ```
  3. Watch $O_2$ pressure reach $\ge 95\text{ kPa}$ and refined ore reach $\ge 250\text{ t}$, triggering the `colony_quota_cutscene` across both the 1st-person and RTS cameras simultaneously!

### 3. Run the Automated End-to-End Verification Pass
```powershell
.\bin\godot.windows.editor.x86_64.console.exe --headless --path demos\outpost_meridian -s res://build_project.gd
.\bin\godot.windows.editor.x86_64.console.exe --headless --path demos\outpost_meridian -- --verify-meridian
```
