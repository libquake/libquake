# Loading

All functions throw `std::runtime_error` on failure.

## LoadBsp

`#include <quakelib/bsp/bsp_level.h>`

```cpp
Level LoadBsp(const std::string &path, const BspLoadOptions &options = {});
```

Reads a version 29 bsp and its `.lit` file if there is one.

| `BspLoadOptions` | Default | |
|---|---|---|
| `bool loadTextureData` | `true` | fill `TextureInfo::rgba` and `emission` |
| `bool convertToOpenGL` | `false` | see `ConvertToOpenGL` |
| `std::map<std::string, SurfaceType> faceTypes` | empty | by texture name, case insensitive |

## ParseMap

`#include <quakelib/map/map_document.h>`

```cpp
MapDocument ParseMap(const std::string &path);
MapDocument ParseMapText(const std::string &text);
```

Reads Quake standard and Valve 220 brushes. Error messages contain the line number.

| `MapDocument` | |
|---|---|
| `std::vector<MapEntity> entities` | |
| `std::vector<std::string> textures` | in order of first use |
| `std::vector<std::string> wads` | worldspawn `wad` key, file names without path |

| `MapEntity` | |
|---|---|
| `LevelEntity properties` | classname and keys, `geometry` is -1 |
| `std::vector<MapBrush> brushes` | |
| `int line` | line of the opening brace |
| `bool IsBrushEntity() const` | has brushes or is the worldspawn |

| `MapBrush` | |
|---|---|
| `std::vector<MapFace> faces` | |
| `int line` | |

| `MapFace` | |
|---|---|
| `std::array<std::array<double, 3>, 3> points` | the three plane points |
| `std::string texture` | |
| `bool valve` | Valve 220 face |
| `float offsetU`, `offsetV`, `rotation`, `scaleX`, `scaleY` | |
| `std::array<float, 4> uAxis`, `vAxis` | Valve 220 only: axis and offset |

## BuildMap

`#include <quakelib/map/map_level.h>`

```cpp
Level BuildMap(const MapDocument &doc, const MapBuildOptions &options = {},
               LightmapLayout *layoutOut = nullptr);
```

`layoutOut` receives the planar lightmap layout. It stays untouched with `XAtlas` uvs.

| `MapBuildOptions` | Default | |
|---|---|---|
| `bool csg` | `true` | remove faces hidden by other brushes of the same entity |
| `bool convertToOpenGL` | `false` | applied after building and baking |
| `std::function<std::pair<int, int>(const std::string &)> textureSize` | none | width and height by texture name |
| `std::map<std::string, SurfaceType> faceTypes` | empty | by texture name without path, case insensitive |
| `LightmapUVOptions lightmapUV` | planar | see [Lighting](lighting.md#lightmapuvoptions) |
| `bool bake` | `false` | needs planar uvs |
| `std::optional<BakeOptions> bakeOptions` | from worldspawn | |
| `std::vector<BakeLight> extraLights` | empty | Quake coordinates |
| `BakeOccluders extraOccluders` | empty | Quake coordinates |
| `PivotMode pivot` | `MapOrigin` | pivot of brush entities, see [Maps](../guide/map.md#brush-entities) |
| `bool mergeGroups` | `false` | one geometry per TrenchBroom group, see [Maps](../guide/map.md#brush-entities) |
