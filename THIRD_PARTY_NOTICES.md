# Third-party notices

BrazenWorks itself is MIT-licensed (see [LICENSE](LICENSE)). It depends on, or
includes code from, the projects below. Keep this file up to date: **any code
copied or adapted from another project must be listed here before it is merged.**

## Libraries (linked, not copied)

### Open CASCADE Technology (OCCT)
- License: GNU LGPL 2.1 with the Open CASCADE exception
- Source: https://github.com/Open-Cascade-SAS/OCCT
- Linked dynamically. Distributed builds must let users replace the OCCT
  libraries (ship them as separate shared libraries, not statically linked).

### Qt 6
- License: GNU LGPL 3.0 (open-source edition)
- Source: https://www.qt.io/download-open-source
- Linked dynamically. Same replaceability requirement as OCCT.

### MitCad solver (Rust crate `mitcad-solver`, with `mitcad-vec3`)
- License: MIT
- Source: https://github.com/mitcad/Mitcad (`core/solver`, `core/vec3`)
- Used as a dependency, pinned to a release tag in `crates/model_core/Cargo.toml`.
  The 2D sketch constraint solver; only `crates/model_core/src/sketch/solver_adapter.rs` uses it.

```
MIT License

Copyright (c) 2026 MitCad contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### CXX (Rust crate)
- License: MIT OR Apache-2.0
- Source: https://github.com/dtolnay/cxx

## Code adapted from other projects

Add one entry per project. Mark each adapted file with a header comment naming
the source file and project, and paste the project's license text below.

### MitCad
- License: MIT
- Source: https://github.com/mitcad/Mitcad
- Files adapted: *none yet*

### SolidCraft
- License: MIT OR Apache-2.0
- Source: https://github.com/tangency-labs/solidcraft
- Files adapted: *none yet*

<!-- When the first file is adapted, paste that project's full LICENSE text here. -->
