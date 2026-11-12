# Level

`#include <quakelib/level.h>`, namespace `quakelib`.

## Level

| Member | |
|---|---|
| `std::vector<LevelEntity> entities` | in file order, the worldspawn is usually first |
| `std::vector<EntityGeometry> geometry` | indexed by `LevelEntity::geometry` |
| `std::vector<TextureInfo> textures` | every texture used by the geometry |
| `LightmapLayers lightmap` | empty for maps built without `bake` |
| `std::vector<std::string> wads` | file names from the worldspawn `wad` key, without path |
| `const LevelEntity *Worldspawn() const` | `nullptr` if there is none |
| `int FindTexture(const std::string &name) const` | index into `textures`, case insensitive, -1 if missing |

## LevelEntity

| Member | |
|---|---|
| `std::string classname` | |
| `std::map<std::string, std::string> attributes` | all keys, including `classname` |
| `int geometry` | index into `Level::geometry`, -1 for point entities |
| `bool Has(key)` | |
| `const std::string &Get(key)` | empty string if missing |
| `float Float(key, fallback = 0)` | |
| `int Int(key, fallback = 0)` | |
| `Vec3 Vector(key, fallback = {0, 0, 0})` | three numbers, missing ones are 0 |
| `Vec3 Origin()` | `origin` |
| `Vec3 Angles()` | pitch, yaw, roll from `angles`, or yaw from `angle` |
| `Vec3 MoveDir()` | Quake's `SetMovedir`: `angle` -1 is up, -2 is down |
| `TargetName()`, `Target()`, `SpawnFlags()` | |

Values are never converted to OpenGL coordinates.

## EntityGeometry

| Member | |
|---|---|
| `std::vector<RenderMesh> meshes` | one per texture |
| `std::vector<CollisionHull> hulls` | one per brush, maps only |
| `Vec3 pivot` | placement of the entity, meshes and hulls are relative to it |
| `Vec3 boundsMin`, `boundsMax` | of the hulls, or of the meshes without hulls |

## RenderMesh

| Member | |
|---|---|
| `std::string textureName` | |
| `int textureWidth`, `textureHeight` | 0 if unknown |
| `std::vector<Vertex> vertices` | |
| `std::vector<uint32_t> indices` | triangle list |
| `SurfaceType type` | `SOLID`, `CLIP`, `SKIP` or `NODRAW` |

## Vertex

`#include <quakelib/vertex.h>`

| Member | |
|---|---|
| `Vec3 point` | |
| `Vec3 normal` | |
| `Vec2 uv` | in texture repeats |
| `Vec2 lightmap_uv` | 0..1 |
| `Vec4 tangent` | xyz, bitangent sign in w |

`Vec2`, `Vec3` and `Vec4` are small float vectors with `[]` access, see `quakelib/qmath.h`.

## CollisionHull

| Member | |
|---|---|
| `Type type` | `Solid`, `Clip` (only clip faces) or `Liquid` (a `*` texture) |
| `std::vector<Vec3> points` | corners without duplicates |
| `std::vector<uint32_t> indices` | triangles, same winding as the meshes |

## TextureInfo

| Member | |
|---|---|
| `std::string name` | |
| `int width`, `height` | size of `rgba`. Sky: one layer, half the miptex width |
| `std::vector<uint8_t> rgba` | empty for maps and bsp files without texture data. Sky: the back layer |
| `std::vector<uint8_t> skyFront` | sky only: the cloud layer, index 0 transparent, empty otherwise |
| `bool hasEmission` | has fullbright pixels |
| `bool hasAlpha` | name starts with `{`, index 255 is transparent, draw with alpha test. Set for maps too |
| `std::vector<uint8_t> emission` | RGBA, black except the fullbright pixels, empty without them |

## Functions

`uint32_t ClassifyTexture(const std::string &textureName)`
:   `TextureFlags` from the name alone, case insensitive, a path is ignored: `TEXTURE_SKY` (`sky...`),
    `TEXTURE_ALPHA` (`{...`), `TEXTURE_LIQUID` (`*...`). `IsSkyTexture`, `IsAlphaTexture` and
    `IsLiquidTexture` test one flag. `#include <quakelib/texture_name.h>`, included by `level.h`.

`SurfaceType DefaultSurfaceType(const std::string &textureName)`
:   `CLIP` for `clip` and `trigger`, `SKIP` for `skip`, `hint`, `hintskip`, `nodraw`, `caulk` and
    `origin`, `SOLID` otherwise. Case insensitive.

`void ConvertToOpenGL(Level &level)`
:   `(x, y, z)` to `(x, z, -y)` for vertices, normals, tangents, hulls, pivots and bounds. Entities are
    not touched.
