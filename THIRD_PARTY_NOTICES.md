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
