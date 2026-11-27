# BrazenWorks

**Open-source CAD/CAM for design and machining.**

BrazenWorks is a free, open-source parametric CAD/CAM application with a familiar
workspace layout: a workspace switcher and tool ribbon across the top, a browser tree
on the left, the 3D viewport with a view cube in the centre, and a feature timeline
along the bottom. One app covers design (sketch, model, assemble) and manufacturing
(setups, toolpaths, simulation, post-processing).

![The BrazenWorks design workspace: browser tree, 3D viewport with view cube, and feature timeline](docs/screenshots/m0-design.png)

> **Status: early development (milestone M0).** The app shell, Open CASCADE viewport,
> and a parametric timeline with extrude/cut/fillet work. It is not usable for real
> work yet. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Tech stack

| Layer | Language | What it does |
|---|---|---|
| `src/app` | C++20 + Qt 6 | Desktop UI: ribbon, browser, viewport, timeline |
| `src/geometry` | C++20 | Thin facade over [Open CASCADE Technology](https://dev.opencascade.org) (B-rep kernel) |
| `src/bridge` | C++ ⇄ Rust | [CXX](https://cxx.rs) bridge between the model and the kernel |
| `crates/model_core` | Rust | Document model, feature timeline, sketches and profiles (MitCAD's solver behind an adapter); later CAM logic |

Why this split: OCCT and Qt are C++, so the UI and kernel wrapper stay in C++ with
no translation layer. The model logic, where most of the tricky state lives, is in
Rust. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Building

Each platform has a one-time setup script in `tools/dev-env/`. It installs the
dependencies and writes `CMakeUserPresets.json`, so building is the same three
commands everywhere: configure, build, test with the `dev` preset
(RelWithDebInfo; use `release` for an optimized build).

### Windows

Requirements: Visual Studio 2022 or newer with the *Desktop development with C++*
workload. `setup-windows.ps1` installs Git, CMake, Ninja, Python and Rust (via
winget, if missing), Open CASCADE (via vcpkg, compiled once, 30–60 min) and Qt 6
(via aqtinstall, prebuilt). It also writes `CMakeUserPresets.json`. Run it from an
**administrator** PowerShell. `msvc.cmd` runs a command inside the MSVC
environment with Qt on `PATH`:

```bat
powershell -ExecutionPolicy Bypass -File tools\dev-env\setup-windows.ps1
tools\dev-env\msvc.cmd cmake --preset dev
tools\dev-env\msvc.cmd cmake --build --preset dev
tools\dev-env\msvc.cmd ctest --preset dev
tools\dev-env\msvc.cmd build\dev\brazenworks.exe
```

Options: `-VcpkgRoot` (default `C:\vcpkg`, keep it short), `-QtRoot` (default
`C:\Qt`), `-QtVersion` (default `6.8.3`). The script is safe to re-run. Each build
copies Qt next to the exe (windeployqt), so `build\dev\brazenworks.exe` also starts
with a double-click. The app
needs OpenGL 2.1 or newer, which any GPU from the last decade has.

### Linux (Ubuntu 24.04 / Debian)

```bash
tools/dev-env/setup-linux.sh          # apt packages + Rust + presets
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/brazenworks
```

### macOS

```bash
tools/dev-env/setup-macos.sh          # Homebrew packages + Rust + presets
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
open build/dev/brazenworks.app
```

### Rust-only tests

The model crate's tests run without Qt or OCCT, against a mock kernel:

```bash
cargo test --manifest-path crates/model_core/Cargo.toml
```

## Units

New documents use **inches**. Change a document's units from the **in ▾** button at the
bottom right, or **Browser → Document Settings → Units**: inches, feet, millimeters,
centimeters or meters. The choice is remembered for new documents.

Length fields accept any unit and mix them; a bare number is in the document unit:
`2`, `1 1/4`, `3/8"`, `1' 6"`, `25mm`, `2in + 5mm`. Internally everything is stored in
millimeters, so switching units never changes the geometry.

## Sketching

1. **Create Sketch** (first tool in the ribbon), then click one of the three origin
   planes, or pick it in the dialog. The view turns to face the plane.
2. Draw with the **SKETCH** tab or keys: **L** line, **R** rectangle, **C** circle,
   **A** 3-point arc, **P** point. After the first click, type a size (or press **Tab**)
   and **Enter**: line length, circle diameter, rectangle width **Tab** height; the
   dimension is added for you. Clicks snap to points, the origin and curves; lines
   within a few degrees of level or plumb get Horizontal/Vertical. Double-click, right-
   click or **Esc** ends a line chain.
   The **CREATE** menu has the rest: 3-point and centre rectangles, 2- and 3-point
   circles, centre and tangent arcs, polygon (type the radius, **Tab**, the sides),
   ellipse, slot and fit-point spline (double-click or **Enter** ends it; click the first
   point to close it). Clicks also snap to line midpoints, line extensions and alignment
   with other points (dashed guides).
3. **D** dimensions: click a curve then empty space (line length, circle diameter, arc
   radius, ellipse major/minor radius), or two things: points (distance), a point and a
   line, two lines (angle, or the gap when parallel), or a line, point or circle and a
   circle (the gap). Type a value in any unit (`2 1/4`, `30mm`) and press **Enter**.
   Double-click a dimension to change it. The first dimension scales the whole sketch.
   A dimension the sketch already implies can be added as a **driven** one, shown in
   brackets; right-click a dimension to switch it between driven and driving.
4. Constraints: pick a tool in **CONSTRAINTS**, then the geometry (or select first, then
   the tool). Refused constraints say why. **Auto Constrain** adds what the drawing
   already shows (joined ends, level lines, tangent arcs).
5. **MODIFY**: **Fillet** and **Chamfer** (click a corner, type the size), **Trim**
   (**T**), **Extend** and **Break** (click the curve), **Offset** (**O**: click a curve,
   its chain comes along, click the side, type the distance), **Move/Copy**, **Rotate**
   and **Sketch Scale** (pick curves or select first, **Enter**, click the base point or
   centre; hold **Ctrl** to copy). **Mirror**, the patterns and **Project** (copies body
   edges into the sketch, fixed) are in **CREATE**.
6. Colours: **blue** can still move, **white** is fully constrained, **red** conflicts;
   dashed is construction (**X**). Red dots mark open ends that keep a profile from
   closing. The status bar shows the degrees of freedom left.
7. **S** / Esc returns to select: click to select, drag points or lines, **Delete**.
8. **Finish Sketch**, then **Extrude**: click profiles (they light up), set the distance,
   direction and Join/Cut/New Body; the result previews live.

The **Sketch Palette** on the right shows the degrees of freedom, display settings
(construction, snap, dimensions, constraints, points, profiles), every tool, and
**Finish Sketch** / **Cancel Sketch** (discards everything since the sketch opened).

Double-click a sketch in the timeline or Browser to edit it later. **Ctrl+Z** /
**Ctrl+Y** undo and redo anything.

## Preferences

The gear at the top right opens **Preferences**: theme (dark or light), default units,
middle-button orbit or pan, wheel direction, camera animation, anti-aliasing, surface
quality, grid, and sketch snapping, inference, constraint tags and pick radius. Settings
can be exported and imported as an `.ini` file. The **?** menu has the user guide,
keyboard and mouse overview, diagnostics, updates and feedback.

## Viewport controls

| Action | Input |
|---|---|
| Pan | Middle-drag |
| Orbit | Shift + middle-drag |
| Zoom | Mouse wheel |
| Select (window) | Left-drag |
| Fit all | F6, or the nav bar at the bottom of the view |

The nav bar also has a home view, orbit/pan/zoom modes for left-drag, display modes
(shaded, shaded with edges, wireframe) and a grid toggle.

## Renaming the product

The product name lives in **one file: [`branding.json`](branding.json)**. CMake reads it
and generates `Branding.h`, the executable name, the macOS bundle name and the Linux
desktop entry. No source file contains the name. To rename everything, including
this README:

```bash
python3 tools/rename.py --name "New Name" --dry-run   # preview
python3 tools/rename.py --name "New Name"
rm -rf build && cmake -S . -B build -G Ninja && cmake --build build
```

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first,
especially the rules on reusing code from other projects.

## License

BrazenWorks is released under the [MIT License](LICENSE). It links to Open CASCADE
(LGPL-2.1 with exception) and Qt (LGPL-3.0) dynamically; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

BrazenWorks is an independent project. It is not affiliated with, endorsed by or
sponsored by Autodesk, Inc. All trademarks belong to their owners.
