# Third-party dependencies and optional assets

The standalone `shakti` binary has **no vendored C libraries** in the published tree.

## Linked system libraries

| Library | Purpose | Platform |
|---------|---------|----------|
| libexpat | XML table loading (`load("file.xml")`) | Linux, macOS |
| librdmacm, libibverbs | Optional RDMA IPC | Linux (when dev headers present) |
| libgomp | OpenMP (matrix `@`, vector `dot` / large `sum`) | Linux (default with GCC) |
| libomp | OpenMP (`brew install libomp`) | macOS |
| libpthread, libm, librt, libdl | Runtime | Linux |

## Apple / Microsoft SDKs

macOS builds use system frameworks (Cocoa, Core Audio, Speech, etc.) under their respective platform licenses. Windows/Android tooling in the local-only tree is not part of the published release.
