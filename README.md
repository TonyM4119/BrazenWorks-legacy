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
| `crates/model_core` | Rust | Document model and feature timeline (later: sketch solver, CAM logic) |

Why this split: OCCT and Qt are C++, so the UI and kernel wrapper stay in C++ with
no translation layer. The model logic, where most of the tricky state lives, is in
Rust. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Building

You need CMake ≥ 3.21, a C++20 compiler, Rust (stable, via [rustup](https://rustup.rs)),
Qt ≥ 6.4 and Open CASCADE ≥ 7.6.

### Linux (Ubuntu 24.04 / Debian)

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl-dev libtbb-dev \
  libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
  libocct-visualization-dev libocct-data-exchange-dev libocct-ocaf-dev occt-misc
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/app/brazenworks
```

### macOS

```bash
brew install cmake ninja qt opencascade
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix opencascade)"
cmake --build build
```

### Windows

Install Visual Studio 2022 (C++ workload), Rust, and dependencies with
[vcpkg](https://vcpkg.io): `vcpkg install opencascade qtbase`. Then configure with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.

### Rust-only tests

The model crate's tests run without Qt or OCCT, against a mock kernel:

```bash
cargo test --manifest-path crates/model_core/Cargo.toml
```

## Viewport controls

| Action | Input |
|---|---|
| Pan | Middle-drag |
| Orbit | Shift + middle-drag |
| Zoom | Mouse wheel |
| Select (window) | Left-drag |
| Fit all | F6 |

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
