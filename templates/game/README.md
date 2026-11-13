# Application layout

`main.cpp` implements `retrato_game_main` and configures `ember::app::run`.
The engine supplies the native entry point. `content/` is copied beside desktop
executables. Graphics settings live in `content/engine.toml`; writable game data
uses a per-user directory rather than the current directory.

With a compatible installed SDK:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/actual/path/to/sdk"
cmake --build build --config Release --parallel 2
```

Alternatively set `RETRATO_SOURCE_DIR` to the actual engine source directory.
`GAME_USE_NOVA` and `GAME_USE_LUA` enable optional integrations; the engine must
include those modules. C++ is sufficient by default. `--headless --frames 3`
checks startup without creating a window. Visual Studio executables normally
live in `build/Release`. See the root README for the technical documentation.
