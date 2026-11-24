# BLOOM — Clean-Slate Graphics & UI Architecture Research

The previous `BLOOM v9.0.0` megashader codebase has been archived unchanged for reference, while first-principles architecture redesign lives in `./research/`.

- **Archived Codebase (`v9.0.0`)**: [`archive/v9/README.md`](archive/v9/README.md) (source, shaders, Lean schema, Makefile, and mutation tests).
- **Re-Research Benchmarks, Root Causes & Tracks**: [`research/README.md`](research/README.md).
- **Upstream Substrates (Read-Only Siblings)**: `../FLOW`, `../ECON`, `../SOUND`, `../PHY`.
- **Consumer Boundary**: [`/Users/kuangtalin/Desktop/Games/include/ge_host_if.h`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h) defines the thin, renderer-agnostic host ABI (`BLOOM` has zero compile/link coupling inside `Games`).
