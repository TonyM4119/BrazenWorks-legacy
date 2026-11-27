# Roadmap

Small, shippable milestones. Each one should leave the app usable for something.

## M0: Foundation ✅
- [x] Repo, CMake + Cargo build, CI, single-source branding and rename tool
- [x] Fusion-style shell: workspace switcher, tabbed ribbon, browser, timeline bar
- [x] OCCT viewport in Qt: shaded display, view cube, grid, CAD mouse mapping
- [x] Rust timeline over OCCT via CXX: extrude (join/cut/new body), fillet, rollback
- [x] STEP export

## M1: Sketching ✅
- [x] UI themed to the concept, with anti-aliasing and animations
- [x] Constraint solver (MitCad's, behind our adapter) + weekly update PRs
- [x] Sketch model: planes, lines, arcs, circles, rectangles, constraints,
      dimensions, conflict refusal, drag; sketches as timeline features
- [x] Profile detection: closed loops → faces; extrude from sketch profiles
- [x] Sketch UI: Create Sketch on an origin plane, SKETCH tab with Finish Sketch;
      line, rectangle, circle, 3-point arc, point, construction; snapping and
      horizontal/vertical inference; 12 constraint tools; dimensions typed in any
      unit; drag with the solver; free/fully constrained/conflict colours and DOF
- [x] Extrude: pick profiles in the view, live preview, distance, direction,
      join/cut/new body
- [x] Edit a sketch later (double-click in the timeline or Browser)
- [x] More sketch shapes: ellipse, fit-point spline, slot, polygon, 3-point and
      centre rectangles, 2- and 3-point circles, centre and tangent arcs
- [x] Sketch editing: trim, extend, break, fillet, chamfer, offset, mirror,
      rectangular and circular patterns, move/copy, rotate, scale, project body
      edges (fixed copies), Auto Constrain
- [x] Solver checks: redundant dimensions offered as driven (reference) dimensions,
      curves that would collapse are refused, dimension edits don't flip the
      shape, the first dimension scales the sketch, loose ends are marked
- [x] More constraints and dimensions: smooth (G2), parallel-line distance,
      point/line/circle-to-circle gaps, ellipse radii; midpoint, extension and
      alignment snapping
- [x] Undo/redo of the whole document (Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z)

## M2: Core modeling
- [ ] Revolve; extrude manipulators in the canvas (drag arrow), extrude to faces
- [ ] Sketch on faces and offset planes; projected edges that follow the body
      (needs persistent edge naming)
- [ ] Edit feature → replay; timeline drag-reorder and suppress
- [ ] Native document format (save/open), autosave
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
