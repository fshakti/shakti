## Optional Linked system libraries

| Library | Purpose | Platform |
|---------|---------|----------|
| libX11, libasound | Synth UI | Linux |
| Cocoa, Core Audio, Core Foundation | Synth UI | macOS |
| Speech, AVFoundation | `import talk` | macOS |

`import lissen` uses `curl` on `PATH` for HTTPS to the Lissen API (not linked at build time).

`import rest` uses `curl` on `PATH` for HTTP client requests (not linked at build time). The in-process HTTP server uses BSD sockets.

`import sonicpi` uses `oscsend` (liblo-tools) or `python3` + [`src/lib/osc_send.py`](../src/lib/osc_send.py) for UDP OSC (not linked at build time).

Disable optional components at build time: `SHAKTI_SYNTH=0`, `SHAKTI_TALK=0`, `SHAKTI_IPC=0`, `SHAKTI_RDMA=0`.

## Optional audio sample packs (not distributed)

Sample libraries under `samples/` are **gitignored** and installed locally by the user. They are not part of the Apache-2.0 source release.

| Pack | Install | License |
|------|---------|---------|
| BSR Samples Yellow (BSRSamples001) | User zip → `samples/bsr_yellow/` | Commercial/sample-pack license from the vendor; verify terms before redistribution or release |

See [SAMPLES.md](SAMPLES.md) for layout and `synth.load_sample()` usage.

