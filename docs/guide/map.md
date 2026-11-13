# Building a map

A `.map` is loaded in two steps. `ParseMap` reads the text into a `MapDocument` (entities, brushes, faces).
`BuildMap` turns the document into a `Level`.

```cpp
--8<-- "examples/build_map.cpp:parse"
```

Standard Quake and Valve 220 faces are detected per face, so mixed files work. Quake 2 surface flags are
read and ignored. Quake 3 and Doom 3 brush formats are rejected with an error.

The document is useful on its own: `doc.textures` lists every texture in order of first use and `doc.wads`
the wads from the worldspawn, enough to load the textures before building.

## Texture sizes

Texture coordinates depend on the texture size, which the map file does not store. `BuildMap` asks for it
through `textureSize`. Unknown textures get uv 0.

```cpp
--8<-- "examples/build_map.cpp:wads"

--8<-- "examples/build_map.cpp:build"
```

`level.textures` of a built map has names and the sizes returned here, no pixels.

## Options

| `MapBuildOptions` | Default | |
|---|---|---|
| `csg` | `true` | remove faces hidden inside other brushes of the same entity |
| `convertToOpenGL` | `false` | y up, applied at the end |
| `textureSize` | none | see above |
| `faceTypes` | empty | surface type by texture name, on top of `DefaultSurfaceType` |
| `lightmapUV` | planar, 16 units per luxel | how `lightmap_uv` is made, see [Lighting](lighting.md#lightmap-uvs) |
| `bake` | `false` | bake the map lights into `level.lightmap` |
| `bakeOptions` | from worldspawn | overrides the worldspawn light settings |
| `extraLights` | empty | lights baked in addition to the map's own |
| `extraOccluders` | empty | extra shadow casting triangles |

Texture names in `faceTypes` are matched without path and case, so `"clip"` also covers `"tools/CLIP"`.

## Collision hulls

Every brush becomes one convex hull with its own points and triangles.

```cpp
--8<-- "examples/build_map.cpp:hulls"
```

| Type | Brush |
|---|---|
| `Solid` | everything else |
| `Clip` | only `CLIP` faces (`clip`, `trigger` or your own `faceTypes`) |
| `Liquid` | a texture starting with `*` |

Brushes made only of `hint`, `hintskip` and `origin` get no hull. A trigger brush is a `Clip` hull, use
the entity's classname to decide what it does.

## Brush entities

Each brush entity gets its own `EntityGeometry`, its meshes and hulls relative to the entity's pivot.
`MapBuildOptions::pivot` chooses it:

| `PivotMode` | Pivot |
|---|---|
| `MapOrigin` (default) | `0 0 0`, the geometry stays in world coordinates |
| `Center` | center of the entity's bounds |
| `BottomCenter` | center of the bottom of the bounds, for props that stand on the floor |
| `BottomCorner` | the lowest corner of the bounds (smallest x, y and z), for modular pieces that snap to a grid |

An entity's key `_pivot` (`origin`, `center`, `bottom` or `corner`) overrides the option, and an `origin` brush wins
over both, as in the Quake tools: its center is the pivot. The worldspawn always stays at `0 0 0`. Lightmap
layout and bake work in world coordinates, the geometry moves to the pivot after them.

CSG only removes faces between brushes of the same entity. `func_group` and `func_detail` are separate
entities and are not merged with the world.
