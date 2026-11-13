# Contributing

Keep changes incremental and retain C++17. Add regression coverage for ownership,
serialization, generated code and shared rendering contracts. Optional features
must not become required dependencies of the minimal runtime. Do not add host-only
SIMD flags to common targets.

Run the CPU configure/build/CTest preset before submitting changes. For rendering,
check available CPU/OpenGL/Vulkan paths. State the tested OS, architecture,
compiler and driver; include reproduction steps and known limits. A workflow file
is not proof a platform has passed. Update README when behavior or public APIs change.

Do not commit build trees, credentials, private logs, system libraries or local
SDKs. Preserve third-party licenses and document changes to vendored dependencies.
The public account name and a GitHub noreply address are sufficient commit identity;
do not include workstation usernames or a private email in project files.
