# Third-party dependencies and optional assets

The standalone `shakti` binary has **no vendored C libraries** in the published tree.

## Linked system libraries

| Library | Purpose | Platform |
|---------|---------|----------|
| libexpat | XML table loading (`load("file.xml")`) | Linux, macOS |
| libX11, libasound | Synth UI | Linux |
| Cocoa, Core Audio, Core Foundation | Synth UI | macOS |
| Speech, AVFoundation | `import talk` | macOS |
| librdmacm, libibverbs | Optional RDMA IPC | Linux (when dev headers present) |
| libgomp | OpenMP (matrix / reduce paths) | Linux (default with GCC) |
| libomp | OpenMP (`brew install libomp`) | macOS |
| libpthread, libm, librt, libdl | Runtime | Linux |

`import lissen` uses `curl` on `PATH` for HTTPS to the Lissen API (not linked at build time).

Disable optional components at build time: `SHAKTI_SYNTH=0`, `SHAKTI_TALK=0`, `SHAKTI_IPC=0`, `SHAKTI_RDMA=0`.

## Optional audio sample packs (not distributed)

Sample libraries under `samples/` are **gitignored** and installed locally by the user. They are not part of the Apache-2.0 source release.

| Pack | Install | License |
|------|---------|---------|
| BSR Samples Yellow (BSRSamples001) | User zip → `samples/bsr_yellow/` | Commercial/sample-pack license from the vendor; verify terms before redistribution or release |

See [SAMPLES.md](SAMPLES.md) for layout and `synth.load_sample()` usage.

## Apple / Microsoft SDKs

macOS builds use system frameworks (Cocoa, Core Audio, Speech, etc.) under their respective platform licenses. Windows/Android tooling in the local-only tree is not part of the published release.
