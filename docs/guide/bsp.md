# Loading a bsp

Quake 1 bsp files (version 29) are supported, BSP2 is not. A `.lit` file next to the bsp is picked up for colored light.

```cpp
--8<-- "examples/load_bsp.cpp:load"
```

## Entities and geometry

Every entity with a model (`worldspawn` and `"model" "*n"`) has a `geometry` index. Point entities have
`-1`.

```cpp
--8<-- "examples/load_bsp.cpp:entities"
```

Tool textures keep their meshes, check `mesh.type` before drawing. `clip` and `trigger` are `CLIP`,
`skip`, `hint`, `nodraw`, `caulk` and `origin` are `SKIP`. More names can be mapped with
`BspLoadOptions::faceTypes`.

`EntityGeometry::pivot` is where the entity is placed in the world. It is the `origin` key for brush
models compiled with an origin brush (rotating entities) and zero for everything else, whose vertices are
already in world space.

## Entity values

`LevelEntity` keeps all keys as strings. The accessors parse on demand:

```cpp
--8<-- "examples/load_bsp.cpp:player"
```

`Angles()` reads `angles` or falls back to `angle`. `MoveDir()` is Quake's `SetMovedir`, including
`-1` (up) and `-2` (down).

## Textures

`level.textures` holds the embedded textures in RGBA, indexed by name with `level.FindTexture(name)`.
Index 255 is transparent on `{` textures. Textures with fullbright pixels have an emission map, see
[Textures](textures.md#emission).

A bsp compiled with `-notex` has names and sizes but no pixels. Load the pixels from the wads in
`level.wads` instead.

Set `loadTextureData = false` to skip the pixels.

## Limits

Collision hulls are not generated for bsp files. Visibility data (PVS) is not read.
