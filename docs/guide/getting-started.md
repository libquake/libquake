# Getting started

## Building

libquake builds with CMake 3.10+ and a C++20 compiler. Add it as a subdirectory and link the `quakelib`
target:

```cmake
set(QLIB_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(QLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/libquake)

target_link_libraries(my_engine PRIVATE quakelib)
```

`quakelib` is a shared library. Headers are under `include/quakelib/`.

| Option | Default | |
|---|---|---|
| `QLIB_BUILD_VIEWER` | `ON` | qviewer, uses an installed raylib or fetches it |
| `QLIB_BUILD_TESTS` | `ON` | unit tests |
| `QLIB_BUILD_EXAMPLES` | `OFF` | the programs in `examples/`, the longer snippets in this guide come from there |

## The Level

Both file types load into the same structure, `quakelib::Level`:

```cpp
#include <quakelib/bsp/bsp_level.h>   // LoadBsp
#include <quakelib/map/map_level.h>   // ParseMap, BuildMap
```

- `entities`: every entity with its key/value pairs. Brush entities point into `geometry`.
- `geometry`: per brush entity one mesh per texture, collision hulls (map only) and bounds.
- `textures`: name, size, and for bsp files the RGBA pixels.
- `lightmap`: one RGBA layer per light style.
- `wads`: the wad files named in the worldspawn.

A `.bsp` is ready to use after `LoadBsp`. A `.map` is parsed with `ParseMap` and turned into a level with
`BuildMap`, which needs texture sizes for the uvs and can bake the lights.

## Coordinates and units

Everything is in Quake units with z up, as stored in the files. Set `convertToOpenGL` in the load or
build options to get y up, or call `ConvertToOpenGL(level)` later. It maps `(x, y, z)` to `(x, z, -y)` for
vertices, normals, tangents, hulls and pivots.

Entity values are never converted. `LevelEntity::Origin()` returns Quake coordinates even in a converted
level, convert them yourself where needed.

32 Quake units are roughly one meter. The player is 56 units tall.

## Vertices

| Field | |
|---|---|
| `point`, `normal` | position and face normal |
| `uv` | texture coordinates in repeats, matching the row order of the texture pixels |
| `lightmap_uv` | 0..1 in the lightmap atlas |
| `tangent` | xyz and the bitangent sign in w |

Indices are triangle lists into the vertices of the same mesh.

## Errors

The C++ functions throw `std::runtime_error` with a readable message (file name, and for `.map` files the
line). The [C API](c-api.md) returns error codes instead.
