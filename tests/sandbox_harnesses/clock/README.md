# Gate 1 multi-clock host

Run from the repository root on Windows with the tests-enabled editor, Vulkan and an active audio output.

```powershell
python tests/sandbox_harnesses/clock/verify.py
```

Pass `--engine PATH` to test another build. Each invocation writes its report, per-process logs and isolated Godot caches under `bin/phase8/clock/<run_id>/`. The JSON names the executable SHA-256, game PIDs, rendering device, audio driver, individual input observations and pass conditions.

The host starts a fresh game for each mode at 640 × 360, VSync off and a 240 FPS cap. A registered 20 Hz simulation module accumulates delivered tick deltas at 50×, then a second process tests the paused clock at 0×. The probe compares module deliveries and clock ticks over seven seconds. A moving Jolt rigid body records the actual backend type and integration step on every callback. Its displacement and callback count must agree with 1 m/s motion at 60 Hz. The wall-clock check allows less than two ticks of endpoint error.

A 250 Hz PCM stream plays through the active audio device. The probe checks the global playback scale every frame, the player's pitch and the change in the actual mixer playhead. The allowed deviation from wall time is 1% to accommodate the audio buffer boundary. This measures engine playback through the active driver, without acoustic capture.

After the terminal acquires focus, an external Python process posts 52 key presses to that game's verified HWND. It types two passes through the lowercase alphabet, starting with an empty command line. The delay between presses varies independently of the frame clock. Each key's expected text must arrive exactly once through the terminal's own `LineEdit`. Windows translates each press before the host posts the release.

The latency clock starts immediately before the host calls `PostMessageW(WM_KEYDOWN)`. It stops at host receipt of loopback acknowledgements for the text edit and the subsequent `RenderingServer.frame_post_draw`. Both maxima must be strictly below 16.6 ms across all 104 presses. The measurement includes Windows message queuing, Godot input routing and host acknowledgement scheduling. The second measurement also includes waiting for the rendered frame callback. Display scanout and physical keyboard transport are outside these endpoints. The host checks the rendered game window's command-line region before and after typing, outside the timed input path.

## Windows result

| Clock mode | Module ticks | Jolt Hz | Audio seconds per wall second | Peak edit | Peak post-draw | First terminal attachment |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 50× | 7,004 | 60.037 | 1.0018 | 9.405 ms | 11.114 ms | 870 ms |
| Paused | 0 | 60.039 | 0.9972 | 8.823 ms | 10.500 ms | 694 ms |

Both modes used the GeForce GTX 1050 Ti and WASAPI. Jolt supplied the same 1/60 s integration step throughout. Every audio scale sample was 1.0, player pitch read 1.0 at the end of both runs, and 3,071 command-line pixels changed in each rendered view. Both runs used executable SHA-256 `0a0c54b0710e020df10ca05ea948c36dead3240f029c39c0a00bb07b6b6533fa`. [Final measurements](results/gate1.json)

Before the first terminal prepared the shared printable-ASCII font atlas, first attachment took 168–169 ms and peak key-to-frame latency was 27.232 ms at 50× and 25.725 ms while paused. An intermediate test with the same editor and explicit glyph preparation measured 12.291 ms and 14.342 ms respectively. The production fix pays the shared atlas construction cost on first terminal attachment. The final hardware run used a quiet build machine. [Pre-fix measurements](results/before-glyph-preparation.json)
