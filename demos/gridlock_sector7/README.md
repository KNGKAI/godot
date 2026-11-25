# Gridlock: Sector 7 — Modular 3D Sandbox Framework Showcase

**Gridlock: Sector 7** is an interactive 3D cyberpunk district vertical slice demonstrating all 7 modules of the Godot 4 Modular 3D Sandbox Framework ([SPEC.md](../../SPEC.md)):

| Module | Showcase Implementation in `demos/gridlock_sector7/` |
| :--- | :--- |
| **`sandbox_core`** | `WorldState` typed scalar channels (`power.*`, `traffic.*`, `mission.*`), `EventBus` pub-sub alerts, `CommandBus` permission-tiered domain commands (`power.status`, `power.diagnose`, `power.coolant`, `power.vent_corp`, `traffic.override`), and [sandbox_schema.tres](resources/sandbox_schema.tres). |
| **`sandbox_sim`** | Two live `SimModule` resources ([power_grid_sim.tres](resources/power_grid_sim.tres) and [traffic_crowd_sim.tres](resources/traffic_crowd_sim.tres)) computing substation thermal runaway, cooling valve physics, and 200-agent traffic flow, plus deterministic `sim.rewind` ring-buffer snapshots. |
| **`sandbox_narrative`** | Branching [mission_sector7_blackout.tres](resources/mission_sector7_blackout.tres) with converging repair (`obj_open_coolant_valve`) vs. corporate hack (`obj_vent_corp_grid`) routes, lifecycle sequences ([seq_blackout_alert.tres](resources/seq_blackout_alert.tres), [seq_grid_restored.tres](resources/seq_grid_restored.tres)), `SandboxActor` player/substation adapters, and a 3-track custom `AnimationPlayer` cutscene (`TYPE_WORLD_STATE`, `TYPE_EVENT_BUS`, `TYPE_ACTOR_COMMAND`). |
| **`sandbox_terminal`** | Three diegetic 3D monitors (`TerminalDisplay3D`) with MSDF font rendering, CRT scanline shaders, and distinct permission tiers (`StreetKioskTerminal` [`USER`], `CoolantValveTerminal` [`USER`], and `SubstationAdminTerminal` [`ADMIN`]). |
| **`sandbox_streaming`** | Multi-cell Meso skyline partition ([manifest.tres](baked_cells/meso_1/manifest.tres)) with combined 3D proxy meshes (`*_proxy.res`) and high-detail cell scenes (`*.scn`), driven by the player's `StreamingInvoker` and `OriginShifter`. |
| **`sandbox_bubble`** | 200 simulated agents (80 `cyber_cruiser` vehicles + 120 `street_drone` couriers) transitioning seamlessly across Tier 0 (SoA), Tier 1 (`MultiMesh` GPU proxies), and Tier 2 (pooled `HydratableActor` 3D scenes with dynamic headlights). |
| **`sandbox_editor`** | Pre-populated `_editor_graph_meta` with `GraphFrame` phase groups for the **Narrative Flow** tab, schema-validated `@export_custom` properties for the **Inspector ID Picker** & **3D Eyedropper** (`MarkerKiosk`, `MarkerSubstation`, `MarkerValve`), and live telemetry for the **Live State** & **Console** tabs. |

---

## Running the Project

### 1. Open in the Godot Sandbox Editor
From the repository root, open the project in the editor to explore the **Sandbox** dock (`Assets`, `Narrative Flow`, `Live State`, `HLOD Baker`, and `Console` tabs):

```powershell
.\bin\godot.windows.editor.x86_64.exe --editor --path demos\gridlock_sector7
```

* Click `res://resources/mission_sector7_blackout.tres` or `res://resources/seq_grid_restored.tres` in the **Assets** tab to view and edit the branching mission graph in the **Narrative Flow** tab.
* Press **F5** (Play) in the editor to watch live node execution highlighting (`green` active, `cyan` completed) and real-time SoA / Hydration / Streaming heatmap telemetry in the **Live State** tab.

### 2. Play Interactively
```powershell
.\bin\godot.windows.editor.x86_64.exe --path demos\gridlock_sector7
```

* **Controls:**
  * `W A S D` + `Shift`: Move / Sprint through Sector 7
  * `Right Mouse Button (Hold)`: Look around
  * `Left Mouse Button (Click on 3D Terminal Screen)`: Engage native terminal focus (`Esc` to release focus)
  * `F1` / `F2` / `F3`: Quick-teleport to Street Kiosk (`MarkerKiosk`), Substation Admin Console (`MarkerSubstation`), or Cryo-Coolant Valve (`MarkerValve`)
  * `F4`: Toggle $10\times$ `SandboxClock` simulation fast-forward
  * `F5`: Rewind simulation state by 40 ticks (`SimulationServer.rewind(40)`)

* **Terminal Walkthrough:**
  1. At the **Street Kiosk (`[F1]`)**, click the monitor and run:
     ```text
     power.diagnose
     ```
     *(Try running `power.vent_corp` here—it will be rejected with `ERR_UNAUTHORIZED` because the street kiosk only has `USER` permissions!)*
  2. Choose your resolution branch:
     * **Branch A (Admin Hack Route `[F2]`):** Teleport/walk to the **Substation Core Console (`ADMIN`)** and run:
       ```text
       power.vent_corp
       ```
     * **Branch B (Field Repair Route `[F3]`):** Teleport/walk to the **Aux Cryo-Coolant Valve (`USER`)** and run:
       ```text
       power.coolant true
       ```
  3. Watch the transformer load and temperature drop in real time until `grid.stabilized` fires, triggering the 3-track restoration cutscene, brightening the district streetlights, and synchronizing a green traffic wave (`20 m/s`).

### 3. Run the Automated End-to-End Verification Pass
To rebuild all `.tres`/`.tscn` assets from scratch and run the automated headless verification suite (testing both branches and deterministic rewind):

```powershell
.\bin\godot.windows.editor.x86_64.console.exe --headless --path demos\gridlock_sector7 -s res://build_project.gd
.\bin\godot.windows.editor.x86_64.console.exe --headless --path demos\gridlock_sector7 -- --verify-gridlock
```
