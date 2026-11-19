```text
██╗     ██╗██████╗  ██████╗ ██╗   ██╗ █████╗ ██╗  ██╗███████╗
██║     ██║██╔══██╗██╔═══██╗██║   ██║██╔══██╗██║ ██╔╝██╔════╝
██║     ██║██████╔╝██║   ██║██║   ██║███████║█████╔╝ █████╗
██║     ██║██╔══██╗██║   ██║██║   ██║██╔══██║██╔═██╗ ██╔══╝
███████╗██║██████╔╝╚██████╔╝╚██████╔╝██║  ██║██║  ██╗███████╗
▒▓▓▓▓▓▓░▒▓░▒▓▓▓▓▓▒░ ░▓▓▓▓▓▓░ ░▓▓▓▓▓▓░▒▓░  ▒▓░▒▓░  ▒▓░▒▓▓▓▓▓▓░
░▒▒▒▒▒▒ ░▒ ░▒▒▒▒▒░   ░▒▒▒▒▒   ░▒▒▒▒▒ ░▒   ░▒ ░▒   ░▒ ░▒▒▒▒▒▒
  ░░░░░  ░  ░░░░░      ░░░░     ░░░░  ░    ░  ░    ░  ░░░░░░
    ░        ░          ░        ░                     ░ ░
```

# libquake

C++20 library for Quake 1 `.map`, `.bsp` and `.wad` files. It turns them into meshes per texture,
collision hulls, entities, RGBA textures and lightmaps split by light style, and bakes the lights of
`.map` files without the Quake compile tools.

- `.map`: standard and Valve 220 format, CSG, texture and lightmap uvs, light baking
- `.bsp`: version 29 and BSP2 with `.lit` files, embedded textures, light styles
- `.wad`: WAD2 textures with fullbright emission maps and split sky layers
- C API for bindings, used by the [LibQuake Unity plugin](https://libquake.github.io/unity/)
- engine plugins built on it: [Godot](https://libquake.github.io/godot/), [Unity](https://libquake.github.io/unity/) and [Unreal](https://libquake.github.io/unreal/)

```cpp
#include <quakelib/bsp/bsp_level.h>

quakelib::Level level = quakelib::LoadBsp("maps/start.bsp");
```

Documentation: [libquake.github.io/libquake](https://libquake.github.io/libquake/), built from `docs/`.

## Building

CMake 3.10+ and a C++20 compiler.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/tests/quakelib_unit_tests
```

On Windows with Visual Studio, build with `cmake --build build --config Release`, the binaries land in
`Release` subfolders (`build/bin/tests/Release/quakelib_unit_tests.exe`).

`QLIB_BUILD_VIEWER`, `QLIB_BUILD_TESTS` (both on) and `QLIB_BUILD_EXAMPLES` (off) select what is built
besides the library.

The light baker traces its rays with [Embree](https://www.embree.org/) where `third_party/embree` holds a
prebuilt for the platform (macOS for now), elsewhere with a bvh of its own. `QLIB_EMBREE=OFF` always uses
the own one. Binaries built with Embree ship its license, `third_party/embree/LICENSE.txt`.

## qviewer

A viewer for maps and bsp files in `tools/qviewer`.

```sh
./build/bin/tools/qviewer -m maps/start.map -w wads/
```
