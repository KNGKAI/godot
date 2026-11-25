# Blackout Protocol — Emergency Dispatch, 90 m/s Flight Streaming, & Multi-Sim Showcase

**Blackout Protocol** is an interactive city-scale emergency response and disaster simulator built on the Godot 4 Modular 3D Sandbox Framework ([SPEC.md](../../SPEC.md)). You pilot **Rescue-1**, a high-speed emergency command helicopter equipped with an **in-cockpit diegetic 3D terminal**, coordinating fire brigades, aerial retardant drops, highway contraflow evacuation, and harbor seawall floodgates across a $5.6\text{ km}$ coastal corridor.

---

## Framework Architecture Showcased

| Capability | Implementation in `demos/blackout_protocol/` |
| :--- | :--- |
| **3 Concurrent Priority-Ordered `SimModule`s (`SimulationServer`)** | 1. **Priority 0 — `WeatherStormSim`** ([weather_storm_sim.gd](scripts/weather_storm_sim.gd)): Simulates $58\text{ kt}$ coastal gale winds, rainfall intensity, and $3.4\text{ m}$ harbor storm surge.<br>2. **Priority 1 — `FireSpreadSim`** ([fire_spread_sim.gd](scripts/fire_spread_sim.gd)): Reads wind and rain from `WorldState` to compute urban refinery fire propagation ($78\%$ hazard across $6$ blocks) vs. foam-truck and aerial retardant suppression.<br>3. **Priority 2 — `EvacuationTrafficSim`** ([evacuation_traffic_sim.gd](scripts/evacuation_traffic_sim.gd)): Governs $400$ civilian evacuation vehicles and fire trucks along the coastal highway. |
| **High-Speed ($95\text{ m/s}$) Helicopter Lookahead Streaming (`StreamingInvoker` + `StreamingServer`)** | Rescue-1 ([rescue_chopper_controller.gd](scripts/rescue_chopper_controller.gd)) mounts a `StreamingInvoker` with a $3.5\text{ s}$ velocity-predictive lookahead horizon, streaming $22$ Meso ($256\text{ m}$) coastal blocks ([manifest.tres](baked_cells/coastal_1/manifest.tres)) within a $\le 2.0\text{ ms}$ time-sliced activation budget (measured peak $0.318\text{ ms}$) across multiple $2048\text{ m}$ `OriginShifter` shifts. |
| **In-Cockpit Diegetic 3D MFD (`TerminalDisplay3D`)** | `ChopperCockpitTerminal` is mounted directly inside the moving Rescue-1 cockpit (`USER` tier, `allowed_namespaces = [&"dispatch", &"stream", &"bubble", &"help"]`), paired with ground `ADMIN` terminals at **Coastal Dispatch HQ** (`[F1]`) and **Harbor Seawall Control** (`[F3]`). |
| **400-Agent Evacuation & Fire Brigade (`HydrationBubble`)** | 80 `fire_truck` units ([fire_truck.tscn](scenes/fire_truck.tscn) with emergency sirens) and 320 `evac_sedan` vehicles ([evac_sedan.tscn](scenes/evac_sedan.tscn)) seamlessly transition across Tier 0 (SoA), Tier 1 (`MultiMesh` GPU proxies), and Tier 2 (pooled 3D nodes). |
| **Branching Visual Mission Graph & FLIR Cutscene (`sandbox_narrative`)** | [mission_blackout_emergency.tres](resources/mission_blackout_emergency.tres) links airborne radar recon (`obj_radar_sweep`) into parallel **Fire Suppression** (`obj_suppress_fires`) and **Civil Defense** (`obj_evac_and_floodgates`) branches before converging on **City Containment** (`obj_secure_coastal_city`), triggering the 3-track `city_saved_cutscene`. |

---

## Running the Project

### 1. Open in the Godot Sandbox Editor
```powershell
.\bin\godot.windows.editor.x86_64.exe --editor --path demos\blackout_protocol
```
* Open [mission_blackout_emergency.tres](resources/mission_blackout_emergency.tres) in the **Sandbox -> Narrative Flow** tab to view the 3-phase disaster response graph.
* Press **F5** (Play) in the editor and open the **Sandbox -> Live State** tab to watch per-module microsecond tick timings across all 3 `SimModule`s alongside the live resident cell heatmap as Rescue-1 flies down the coast.

### 2. Play Interactively
```powershell
.\bin\godot.windows.editor.x86_64.exe --path demos\blackout_protocol
```
* **Controls:**
  * `W A S D`: Fly Rescue-1 horizontally ($38\text{ m/s}$ cruise)
  * `Shift (Hold)`: Engage **$95\text{ m/s}$ High-Speed Turbo Flight**
  * `E / Space` and `Q / Ctrl`: Climb / Descend ($22\text{ m/s}$)
  * `Right Mouse Button (Hold)`: Look around cockpit / coastal skyline
  * `Left Mouse Button (Click In-Cockpit MFD or Rooftop Terminal)`: Engage CLI focus (`Esc` to release)
  * `F1` / `F2` / `F3`: Quick-teleport Rescue-1 to **Coastal Dispatch HQ** (`MarkerHelipadHQ`), **Burning Refinery** (`MarkerRefineryFire`), or **Harbor Seawall Control** (`MarkerSeawallControl`)
  * `F4`: Toggle **$20\times$ `SandboxClock` Fast-Forward**
  * `F5`: Execute **Deterministic Multi-Module Rewind** (`SimulationServer.rewind(40)`)

* **Terminal Walkthrough:**
  1. Look down-left inside the cockpit, click the **Rescue-1 Tactical MFD**, and run:
     ```text
     dispatch.radar_sweep
     dispatch.fire_trucks true
     dispatch.airdrop
     ```
  2. At the **Coastal Dispatch HQ Terminal (`[F1]`)**, run:
     ```text
     dispatch.contraflow true
     ```
  3. Fly or teleport (`[F3]`) to the **Harbor Seawall Control Terminal** and run:
     ```text
     weather.floodgate true
     ```
  4. Watch fire hazard drop below $15\%$ and civilian evacuation reach $100\%$, triggering the FLIR cutscene and turning all city hazard beacons green!

### 3. Run the Automated End-to-End Verification Pass
```powershell
.\bin\godot.windows.editor.x86_64.console.exe --headless --path demos\blackout_protocol -s res://build_project.gd
.\bin\godot.windows.editor.x86_64.console.exe --headless --path demos\blackout_protocol -- --verify-blackout
```
