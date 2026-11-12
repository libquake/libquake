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

C++20 library that turns Quake 1 `.map`, `.bsp` and `.wad` files into data an engine can use: meshes per
texture, collision hulls, entities, RGBA textures and lightmaps split by light style. `.map` files can be
lit with a built-in baker, so levels work without running the Quake compile tools.

The [LibQuake Unity plugin](https://libquake.github.io/unity/) is built on it, and the repository ships a
small viewer, [qviewer](tools/qviewer.md).

```cpp
#include <quakelib/bsp/bsp_level.h>

quakelib::Level level = quakelib::LoadBsp("maps/start.bsp");
for (const auto &ent : level.entities) {
  if (ent.geometry >= 0) {
    for (const auto &mesh : level.geometry[ent.geometry].meshes) {
      // mesh.textureName, mesh.vertices, mesh.indices
    }
  }
}
```

Start with [Getting started](guide/getting-started.md). The [reference](reference/level.md) lists every
type and function of the public API.
