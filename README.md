# PaintFromRef

Nuke NDK plugin (Nuke 14.1 – 17.1; Windows, Linux-ready) that recreates a
reference image as paint strokes in a **standalone RotoPaint node**. Once
generated, the RotoPaint carries the image format and all strokes itself —
the original image is no longer needed.

Stroke synthesis follows Hertzmann, *Painterly Rendering with Curved Brush
Strokes of Multiple Sizes* (SIGGRAPH 1998): layered brushes from coarse to
fine, curved strokes that follow image edges, detail strokes only where the
coarser layers are still wrong.

## Usage

1. Connect a reference image to **PaintFromRef** (Draw menu).
2. Pick a **quality** preset (`draft` / `normal` / `fine` / `ultra`), or
   `custom` and tune the Advanced tab.
3. Press **Create RotoPaint**. A new RotoPaint node appears next to the
   PaintFromRef node with the strokes and the correct output format. Delete
   the source and the PaintFromRef node if you like — the RotoPaint stands
   alone. Inputs with a cropped data window are supported (outside the bbox
   is treated as black).

The analysis runs in C++ (sub-second for 512², a few seconds for 2K). The
node is built by generating the RotoPaint `curves` serialization in one
`fromScript()` call (the per-stroke `nuke.rotopaint` API is ~500x slower).

## Multi-version layout

The binary is Nuke-version-specific, so the deployed folder holds one
subfolder per major.minor and `init.py` adds the right one at startup:

```
~/.nuke/PaintFromRef/
  init.py            # picks 14.1/, 15.2/, ... to match the running Nuke
  menu.py            # toolbar registration (GUI)
  pfr_builder.py     # strokes-json -> RotoPaint builder
  14.1/PaintFromRef.dll   15.2/...  16.0/...  16.1/...  17.0/...  17.1/...
```

Registration goes in `~/.nuke/init.py` (not menu.py) so terminal/farm modes
load the plugin too: `nuke.pluginAddPath('./PaintFromRef')`.

## Build & install (Windows)

VS2019 build tools + CMake. Verified against 14.1v8, 15.2v9, 16.0v8,
16.1v1, 17.0v4, 17.1v1 — all build with VS2019 (NDK_NOTES 1.1).

```powershell
# everything at once (configure/build per version, optional -Install):
powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 -Install

# one version by hand:
cmake -G "Visual Studio 16 2019" -A x64 `
      -DNuke_DIR="C:/Program Files/Nuke16.0v8/cmake" -S . -B build-16.0v8
cmake --build build-16.0v8 --config Release
cmake --install build-16.0v8 --config Release --prefix "$env:USERPROFILE/.nuke"
```

Packaging: `scripts\package.ps1` zips all built versions into one
distributable under `dist\`.

## Linux

The sources compile warning-free with gcc 11 (`-Wall -Wreorder`) against the
Nuke 14.1/16.0/17.1 headers (checked in AlmaLinux 9 WSL; Foundry's own
headers emit deprecation warnings on 17.1). CMake gates all MSVC flags and
sets `PREFIX ""` so the output is `PaintFromRef.so`, and the install layout
is identical. Toolchain per Foundry docs: gcc 9 for Nuke 14, gcc 11 for
15–17. Note this proves compile-portability only — the plugin has not been
loaded in a Linux Nuke (see NDK_NOTES 57 for why a stub link proves little).

```bash
cmake -DNuke_DIR=/usr/local/Nuke16.0v8/cmake -S . -B build-16.0v8
cmake --build build-16.0v8
cmake --install build-16.0v8 --prefix ~/.nuke
```

## Headless / scripting

`knob_changed` never fires in terminal mode, so the button has a scripting
mirror — bump the hidden `run_trigger` knob and cook the node:

```python
pf["output_json"].setValue("C:/tmp/strokes.json")  # optional explicit path
pf["run_trigger"].setValue(pf["run_trigger"].value() + 1)
pf.sample("red", 0, 0)          # forces the cook that runs the analysis
import pfr_builder
pfr_builder.build("C:/tmp/strokes.json", pf.name())
```

Tests: `scripts\test_all.ps1` runs `test/test_end_to_end.py` against every
version's build; `test/test_quality_sweep.py`, `test/test_2k.py`,
`test/test_cropped_bbox.py` cover presets, production scale and cropped
data windows. All use `-ti` (interactive license; `-t` needs a render
license). API ground-truth probes live in `probe/`.

## License

MIT — see `LICENSE`. No third-party libraries are bundled.
