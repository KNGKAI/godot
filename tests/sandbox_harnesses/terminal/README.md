# Gate 4 rendered terminal room

From the repository root, run the gate with a Windows editor built with `sandbox_terminal`, Vulkan Forward+, Python 3.10 or later with Pillow 9.1 or later, and Windows English (US) OCR installed.

```powershell
python tests/sandbox_harnesses/terminal/run_gate.py
```

This opens a 1600 × 900 room. It briefly captures the mouse during the physics interaction test and restores it afterward. Supply `--engine PATH` to test another executable. Each run writes its own directory under `runs/` with a runtime log, room screenshot, 64 original framebuffer crops, projected coordinates, rectified OCR inputs, raw OCR readings and a combined `result.json` including the executable SHA-256. The runner exits successfully only when both the rendered checks and OCR pass.

## Windows result, 2026-11-23

On a GeForce GTX 1050 Ti with Vulkan 1.4.312 Forward+, Windows OCR read all 64 titles and returned an empty string for the blank control. All five viewports drew in every trace frame. For eight frames per configuration, each frame recorded one scene and occlusion pass in the ordinary room, five with the four positive-control cameras and 3D enabled, and one after disabling 3D again. The captured pointer landed at (479.84, 183.65) viewport pixels against an expected (480, 183.6). The actor emitted one lock and one unlock, and the focused terminal received one wheel event. The throttled screen drew in zero of four distant frames, zero of four frames facing away, and all four frames after returning to view. [Measurements and all OCR readings](results/result.json).

The unaltered images show the [four-terminal room](results/room.png) and the same title at [1 m head-on](results/Console0-1m-0deg.png), [4 m at 45°](results/Console0-4m-45deg.png) and [8 m at 45°](results/Console0-8m-45deg.png).

## Text samples

Four wall-mounted terminals display different alphanumeric titles at the module's default screen size, resolution and font size. A perspective camera with a 50° vertical field of view targets the middle of each title. It samples every integer distance from 1 through 8 metres, first straight on and then at 45° horizontally from the surface normal. Distance is measured from the eye to the title centre.

The crop contains pixels from the rendered room window. The terminal label's layout bounds locate the region to read. Camera projection and eye-space depth at its corners supply a projective transform to straighten the crop. Pillow enlarges it, adjusts grayscale contrast and adds a white margin. Windows OCR reads the resulting pixels without access to the expected string. The checker ignores whitespace and letter case and requires the remaining text to match on all 64 views. A blank control must produce no characters. This measures recognition of the sampled titles under this camera, resolution, lighting and font configuration. The raw crops retain native contrast and pixel size for human inspection.

## Occlusion and input

The room contains four independent terminal viewports and an occluder. With GPU profiling enabled, the test reads the engine's `> Render Viewport`, `> Render 3D Scene` and `Update Occlusion Buffer` markers from eight distinct frames per configuration. All five viewports must render. The ordinary room must produce one 3D scene and one occlusion update. As a positive control, four active cameras and occlusion buffers are added to the four terminals, and their 3D rendering is enabled. There must then be five scene and occlusion updates. Disabling 3D with those cameras still present must restore one of each. The timestamps count invocations. They do not measure the cost of an individual pass.

The input test injects mouse and keyboard events into the root viewport. A left click at a deliberately off-centre event coordinate must respect a solid body between the camera and the terminal. After removal of that body, the same captured click must lock the actor. Moving the aim point checks UV-space position, delta and velocity. The focused viewport must receive the wheel, the command line must receive typed characters, Enter must submit once to the command bus, and Escape must unlock the actor. The script also dispatches an allowlisted quoted argument to a GDScript handler and checks rejection of a USER command outside the terminal's namespace list. Moving the camera out of range, turning it away and returning it to the display must produce zero, zero and four draws in four frames at each position. Per-viewport GPU timestamps identify the tested screen.
