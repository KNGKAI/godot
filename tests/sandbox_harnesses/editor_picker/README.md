# Native editor picker probe

Run from the repository root with a tests-enabled sandbox editor.

```powershell
python tests/sandbox_harnesses/editor_picker/verify.py bin/godot.windows.editor.x86_64.exe bin/phase7
```

The runner copies this project into a fresh directory and isolates editor settings and caches. Its opt-in EditorPlugin opens the fixture scene, uses the native Inspector to search typed state keys and reads the missing-ID and incompatible-type badges.

It arms the eyedropper on host node properties and on an actor-command animation key. It projects the actor and marker into the editor camera and sends mouse button events through the editor viewport. It verifies the bound properties and the underlying animation payload, including an unrelated payload value. The final check frames the marker and reads the editor selection. The probe runs headless and records logical viewport positions, widget results, and the editor binary hash in `result.json`.
