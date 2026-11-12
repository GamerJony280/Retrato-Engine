# Third-party components

Project-owned code is MIT. Dependencies retain their original licenses and notices.

| Component | Purpose | Distribution / license |
|---|---|---|
| ufbx | Optional OBJ/FBX import and curve baking | Included sources and MIT notice in `third_party/ufbx` |
| cgltf | Optional glTF/GLB reader | Included header and MIT notice in `third_party/cgltf` |
| stb_image | Optional model-image decoding | Included header; MIT alternative in `third_party/stb` |
| RmlUi 6.0, commit `7a06f27db04fe5d13a5dacc19b2b4544673a4eca` | Optional retained interface | Included build sources; MIT, see LICENSE.txt and source notices |
| DejaVu Sans | Interface font | `assets/ui.ttf`; see `assets/FONT-LICENSE.txt` |
| SDL2 | Desktop window/input and GL context | System dependency; zlib license |
| Lua 5.4 | Optional scripting | System dependency; MIT |
| Jolt Physics 5.6 | Optional rigid-body physics | External local source/installed package; MIT; no automatic download |
| FreeType | RmlUi text | System dependency; review FTL/GPL terms for the chosen distribution |
| Vulkan loader / OpenGL | Driver interfaces | System dependencies |
| glslang | Development SPIR-V compilation | Not required at runtime; cached shaders included |

CMake performs no dependency downloads. Project engine code has no built-in
analytics or network reporting. Optional scripts and application code are trusted
code; Lua standard libraries are not a sandbox. No Python, browser engine or
JavaScript runtime is included in the game runtime.

The RmlUi snapshot omits upstream samples/documentation not required to build;
its functional sources and notices are retained. `third_party/IMPORT_SOURCES.md`
records vendored import-source hashes.

`tests/fixtures/sausage.fbx` comes from ufbx's
`data/blender_279_sausage_7400_binary.fbx` under the upstream license preserved in
`third_party/ufbx/LICENSE`. The robot, textures and clips are original project
test resources under MIT. `tools/make_sample.py` requires Python/Pillow only to
regenerate test content; generated assets are already included.

## Local cgltf alignment fix

The vendored cgltf scalar-read helpers use memcpy into local float/int16/uint16/
uint32 values rather than dereferencing typed pointers into potentially unaligned
buffers. This covers attributes, indices, normalized values and sparse paths
using those helpers. It preserves formats and cgltf's public ABI. It does not
replace bounds validation or isolate hostile inputs. Review/reapply this change
when updating upstream.

`tests/fixtures/cgltf_misaligned_read.glb` is an intentionally malformed mutation
of the original robot, retained for regression coverage. It is not a playable asset.
The ufbx build enables its portable sanitizer-safe mode (`UFBX_UBSAN=1`).
