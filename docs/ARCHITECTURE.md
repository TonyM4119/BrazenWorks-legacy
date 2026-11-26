# Architecture

```
┌──────────────────────────── src/app (C++ / Qt 6) ────────────────────────────┐
│ MainWindow: ribbon · browser · timeline bar        Viewport: OCCT V3d in Qt   │
└───────────────┬──────────────────────────────────────────────┬───────────────┘
                │ calls timeline API                            │ displays TopoDS_Shape
                ▼                                               │
┌────────── crates/model_core (Rust) ──────────┐                │
│ Timeline · Feature · (sketch solver, CAM)    │                │
│ talks to geometry only via `trait Kernel`    │                │
└───────────────┬──────────────────────────────┘                │
                │ CXX bridge (src/bridge, ffi.rs)               │
                ▼                                               │
┌────────── src/geometry (C++) ─────────────────────────────────┴───────────────┐
│ Kernel.h: extrude, fuse, cut, fillet, volume, STEP export…                   │
└───────────────┬──────────────────────────────────────────────────────────────┘
                ▼
        Open CASCADE Technology (B-rep kernel, LGPL)
```

## Layers

**`src/geometry` (C++).** The only code that calls OCCT modeling algorithms
(`BRepPrimAPI_*`, `BRepAlgoAPI_*`, `BRepFilletAPI_*`…). Keeping OCCT behind one
facade means a kernel upgrade, or a different kernel, touches one folder.

**`crates/model_core` (Rust).** Owns the design document. A `Feature` is pure:
given the body built so far, it returns the new body. The `Timeline` replays
features up to the rollback marker. Rollback, reordering, suppression and
dimension edits are all "change the list, replay". Geometry goes through the
`Kernel` trait. That lets the model be unit-tested with a mock kernel
(`cargo test`, no C++ needed). Planned here as well: the sketch constraint
solver, the document file format, and CAM toolpath logic.

**Sketch solver.** `crates/model_core/src/sketch/solver_adapter.rs` wraps the
2D constraint solver (MitCad's `mitcad-solver`, MIT, pinned to a release tag)
behind our own types (`ConstraintSolver`, `Rule`, `PointRef`…). It is the only
file that names the library; its tests pin the behaviour we rely on, so a
solver update that changes it fails CI. Updates arrive as pull requests from
`.github/workflows/update-solver.yml`.

**`src/bridge` (C++ ⇄ Rust).** `ffi.rs` declares the bridge with
[CXX](https://cxx.rs): C++ geometry functions the Rust side calls (`geom_*`)
and the Rust timeline API the app calls (`timeline_*`). `GeometryBridge.cpp`
implements the C++ half. CMake runs Cargo, which generates
`model_core/src/ffi.rs.h` for C++ to include.

**`src/app` (C++ / Qt 6).** The UI. `Viewport` renders an OCCT `V3d_View` into a
`QOpenGLWidget` (the approach in OCCT's own Qt sample) and maps the mouse the
CAD-standard way. `MainWindow` builds the workspace layout and calls the Rust
timeline. Floating panels (browser, command dialogs, nav bar) are child
widgets of the viewport: it is deliberately *not* a native window, so Qt can
draw over the 3D view. All colours come from `Theme`, all icons from
`resources/icons` via `Icons`, recoloured per state.

## Branding

`branding.json` → `cmake/Branding.cmake` → generated `Branding.h`, executable
name, bundle name and desktop entry. Code uses `branding::kDisplayName` and
friends and never a string literal of the name. `tools/rename.py` changes the
JSON and the prose docs in one go.

## Why not all-Rust or all-C++?

- OCCT and Qt are C++. Calling them from C++ needs no bindings, and OCCT has no
  mature Rust bindings.
- The model, solver and CAM logic are where most bugs in a CAD app live
  (state, ordering, numerical edge cases). Rust's safety helps most there.
- MitCad uses the same split (Rust model, C++ OCCT + Qt), so its ideas map onto
  this layout directly.
