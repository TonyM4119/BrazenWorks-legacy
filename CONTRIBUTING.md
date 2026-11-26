# Contributing to BrazenWorks

Thanks for helping. This file covers how to build, where code goes, and the
rules on reusing code from elsewhere. Please read the last part carefully.

## Build and test

See the [README](README.md#building). Before opening a pull request:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cargo fmt --manifest-path crates/model_core/Cargo.toml
cargo clippy --manifest-path crates/model_core/Cargo.toml
```

## Where code goes

| You're adding… | Put it in |
|---|---|
| A new modeling operation (OCCT call) | `src/geometry/Kernel.{h,cpp}`, then expose it in `src/bridge` |
| Feature/timeline/document logic, sketch solver, CAM toolpath logic | `crates/model_core` (Rust) |
| UI: panels, dialogs, ribbon tools | `src/app` (C++/Qt) |
| Colours, sizes, the stylesheet | `src/app/Theme.{h,cpp}`: never hard-code a colour in a widget |
| An icon | `resources/icons/<name>.svg`, 24×24, stroke-only, using `currentColor` (see existing icons) |
| Anything with the product name in it | **Nowhere.** Read it from `Branding.h` (C++) or `branding.json` |

Rules of thumb:
- Only `src/geometry` includes OCCT modeling headers. The viewport (`src/app/Viewport.*`)
  is the one exception, for OCCT's visualization classes.
- The Rust crate never calls OCCT directly; it goes through the `Kernel` trait.
- New model logic gets Rust unit tests against the mock kernel in `timeline.rs`.

## Reusing code from other projects

We study and borrow from other open-source CAD/CAM projects, especially
[MitCad](https://github.com/mitcad/Mitcad) (MIT) and
[SolidCraft](https://github.com/tangency-labs/solidcraft) (MIT OR Apache-2.0).
Their licenses allow it as long as we credit them:

1. **Only permissive sources** (MIT, Apache-2.0, BSD, zlib) can be copied
   directly. GPL code must not be copied. LGPL code can only be *linked* as a
   library, never pasted in.
2. **Credit every copied or adapted file.** Add a header comment:
   `// Adapted from MitCad (MIT), path/to/original.cpp, commit <hash>`
   and add the file to [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) in the same PR.
3. **Respect the source project's own contribution rules.** SolidCraft builds
   clean-room from public documentation and asks contributors not to bring in
   proprietary material. Code we take from it must stay that way.
4. **Say so in the PR description** when a PR contains copied or adapted code.

## Nothing from Autodesk

BrazenWorks offers a familiar *workflow*. It does not copy Autodesk's product.

- Never copy Autodesk code, icons, images, file-format internals from
  decompiled software, or documentation text.
- Don't use "Fusion", "Fusion 360" or other Autodesk trademarks in the product,
  file names, UI text or marketing. A factual comparison in docs ("works like
  Fusion's timeline") is fine.
- Draw our own icons. Generic CAD names for tools ("Extrude", "Fillet") are fine.

## Commit messages

Short summary line (≤ 72 chars), blank line, then the why. Reference issues with `#123`.
