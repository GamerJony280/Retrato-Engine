# Changes

## 0.7.0 — initial development publication

- Direct desktop Vulkan/OpenGL presentation through the common API, with explicit capture retained.
- Shared depth contract and `graphics.ps1_depth_test`; classic PS1 remains the default.
- Cached PS1 sort depth and no CPU depth writes/clears when disabled.
- Vulkan D32/D16 selection, lazy download storage and device-limit validation.
- SDL imported-target scope fixes for external source consumers.
- Cue lifecycle cleanup, stable same-time order and callback insertion coverage.
- Fewer repeated Host lookups in arcade collision queries.
- Readable generated Nova C++ names with disjoint encoding for reserved spellings.
- Contract, presentation, CMake scope and relocation regression checks.
- English technical README, Spanish companion and sanitized source-only publication.

Nova module protocol remains 2 and implemented language syntax remains 0.2.
Cross-release C++ binary ABI stability is not guaranteed. Platform/device limits
and measured validation scope are stated in README.
