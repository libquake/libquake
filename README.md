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
- `.bsp`: version 29 with `.lit` files, embedded textures, light styles
- `.wad`: WAD2 textures with fullbright emission maps and split sky layers
- C API for bindings, used by the LibQuake Unity plugin

```cpp
#include <quakelib/bsp/bsp_level.h>

quakelib::Level level = quakelib::LoadBsp("maps/start.bsp");
```

The documentation is in `docs/` ([Zensical](https://zensical.org)): `pip install -r requirements-docs.txt`,
then `zensical serve`.

## Building

CMake 3.10+ and a C++20 compiler.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/tests/quakelib_unit_tests
```

`QLIB_BUILD_VIEWER`, `QLIB_BUILD_TESTS` (both on) and `QLIB_BUILD_EXAMPLES` (off) select what is built
besides the library.

## qviewer

A viewer for maps and bsp files in `tools/qviewer`.

```sh
./build/bin/tools/qviewer -m maps/start.map -w wads/
```
