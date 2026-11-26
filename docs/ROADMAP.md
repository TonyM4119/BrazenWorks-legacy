# Roadmap

Small, shippable milestones. Each one should leave the app usable for something.

## M0: Foundation ✅
- [x] Repo, CMake + Cargo build, CI, single-source branding and rename tool
- [x] Fusion-style shell: workspace switcher, tabbed ribbon, browser, timeline bar
- [x] OCCT viewport in Qt: shaded display, view cube, grid, CAD mouse mapping
- [x] Rust timeline over OCCT via CXX: extrude (join/cut/new body), fillet, rollback
- [x] STEP export

## M1: Sketching
- [x] UI themed to the concept, with anti-aliasing and animations
- [x] Constraint solver (MitCad's, behind our adapter) + weekly update PRs
- [x] Sketch model: planes, lines, arcs, circles, rectangles, constraints,
      dimensions, conflict refusal, drag; sketches as timeline features
- [ ] Profile detection: closed loops → faces; extrude from sketch profiles
- [ ] Sketch UI: draw, constrain, dimension, finish, extrude

## M2: Core modeling
- [ ] Extrude/revolve from sketch profiles, with in-canvas manipulators and the
      floating command dialog (as in the UI concept)
- [ ] Edit feature → replay; timeline drag-reorder and suppress
- [ ] Native document format (save/open), autosave, undo/redo
- [ ] Fillet/chamfer on selected edges, shell, hole, press-pull

## M3: Interop
- [ ] STEP/IGES import, STL/3MF export, send-to-slicer
- [ ] Measure and inspect tools

## M4: Assemblies
- [ ] Components, joints, motion

## M5: Manufacture workspace (CAM)
- [ ] Setups (stock, WCS), tool library
- [ ] 2D: face, contour, pocket, adaptive clearing, drilling
- [ ] Simulation and post processors (GRBL, LinuxCNC first)
- [ ] Review MitCad's CAM (lathe, mill-turn, Carvera posts) before writing our own

## Later
- Drawings, sheet metal, surfaces, simulation, plugin/scripting API, MCP server for AI agents
