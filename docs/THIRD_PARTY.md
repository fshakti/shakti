# Third-party dependencies

The standalone `shakti` build has no vendored C libraries.

System libraries linked on Linux/macOS desktop builds:

| Library | Purpose |
|---------|---------|
| libexpat | XML table loading (`load("file.xml")`) |
| libX11, libasound | synth UI (Linux) |
| Cocoa, Core Audio | synth UI (macOS) |
| Speech, AVFoundation | talk module (macOS) |
| librdmacm, libibverbs | optional RDMA IPC when dev headers present (Linux) |
