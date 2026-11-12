# Lighting

## LightmapLayers

`#include <quakelib/level.h>`

| Member | |
|---|---|
| `int width`, `height` | |
| `std::vector<int> styles` | light style of each layer |
| `std::vector<std::vector<uint8_t>> layers` | RGBA, alpha 0 where the layer has no light |
| `bool Empty() const` | |
| `std::vector<uint8_t> &Layer(int style)` | the layer of a style, created if missing |

## LightStyleAnimator

`#include <quakelib/level.h>`

| Member | |
|---|---|
| `void SetStyle(int style, const std::string &pattern)` | `a` dark, `m` normal, `z` double, 10 letters per second |
| `const std::string &Style(int style) const` | |
| `int Value(int style) const` | current value, `Normal` (264) is `m` |
| `bool Update(double time, const LightmapLayers &)` | advance to `time` in seconds, true if a style used by the layers changed |
| `std::vector<uint8_t> Compose(const LightmapLayers &)` | full RGBA image |
| `bool Recompose(const LightmapLayers &, std::vector<uint8_t> &rgba)` | rewrite the texels of changed styles, true if anything changed |

`const char *DefaultLightStylePattern(int style)` returns Quake's default pattern of a style. Styles 32..62
default to `m`.

## Baking

`#include <quakelib/lightmap_baker.h>`

```cpp
BakedLightmap BakeLightmap(const LightmapLayout &layout, const BakeOccluders &occluders,
                           const std::vector<BakeLight> &lights, const BakeOptions &options = {});

std::vector<BakeLight> QuakeLightsFromEntities(const std::vector<LevelEntity> &entities,
                                               bool openGLCoords = false,
                                               BakeOptions *optionsOut = nullptr);
```

`BuildMap` calls both. Use them directly to bake a layout with your own lights and occluders.
`QuakeLightsFromEntities` writes the worldspawn settings to `optionsOut`.

| `BakeLight` | Default | |
|---|---|---|
| `Type type` | `Point` | `Point`, `Spot` or `Sun` |
| `Falloff falloff` | `Linear` | Quake's `delay` key: `Linear`, `Inverse`, `InverseSquare`, `None`, `LocalMinlight`, `InverseSquareB` |
| `Vec3 position` | | |
| `Vec3 direction` | `{0, 0, -1}` | spot and sun |
| `Vec3 color` | `{1, 1, 1}` | 0..1 |
| `float intensity` | 300 | Quake's `light` value |
| `float distanceScale` | 1 | Quake's `wait` key |
| `float coneAngle` | 40 | spot, full angle in degrees |
| `float softAngle` | 0 | spot, inner angle at full intensity |
| `int style` | 0 | |
| `bool castShadows` | `true` | |

| `BakeOptions` | Default | |
|---|---|---|
| `float angleScale` | 0.5 | 0 ignores the surface angle, 1 is pure cosine |
| `float rangeScale` | 0.5 | `_range` |
| `float minLight` | 0 | `light` / `_minlight` on the worldspawn |
| `Vec3 minLightColor` | `{1, 1, 1}` | |
| `float surfaceOffset` | 1 | distance of the samples from the surface |
| `int threads` | 0 | 0 uses all hardware threads |

| `BakeOccluders` | |
|---|---|
| `std::vector<Vec3> triangles` | 3 points per triangle |
| `std::vector<uint8_t> isSky` | one per triangle, may be empty |

`BakedLightmap` is a `LightmapLayers` with `bakeMilliseconds`.

## LightmapUVOptions

`#include <quakelib/lightmap_layout.h>`

| Member | Default | |
|---|---|---|
| `Method method` | `Planar` | `Planar` or `XAtlas` |
| `float luxelSize` | 16 | planar: map units per texel |
| `float texelsPerUnit` | 0 | xatlas: 0 picks a scale that fits `resolution` |
| `uint32_t resolution` | 0 | xatlas: atlas size hint, 0 is about 1024 |
| `uint32_t padding` | 1 | empty texels around each chart |

## LightmapLayout

`#include <quakelib/lightmap_layout.h>`

```cpp
LightmapLayout BuildLightmapLayout(std::vector<RenderMesh *> meshes,
                                   const LightmapLayoutOptions &opts = {});
```

Builds planar charts for `SOLID` meshes, packs them and writes `lightmap_uv`. Vertices shared between
charts are split. The result is deterministic.

| `LightmapLayout` | |
|---|---|
| `int width`, `height` | atlas size |
| `float luxelSize` | |
| `int padding` | |
| `int unlitX`, `unlitY` | texel used by faces without a chart, baked to 128 |
| `std::vector<LightmapChart> charts` | |

| `LightmapChart` | |
|---|---|
| `uint64_t key` | hash of plane and triangles, stable while the geometry is |
| `Vec3 normal`, `float dist` | plane |
| `Vec3 uAxis`, `vAxis` | in the plane |
| `int gridMin[2]` | world grid index of the first luxel |
| `int width`, `height` | in luxels |
| `int atlasX`, `atlasY` | position in the atlas |
| `std::vector<Vec3> triangles` | world positions |
| `Vec3 LuxelPosition(float i, float j, float luxelSize) const` | world position of a luxel |

| `LightmapLayoutOptions` | Default | |
|---|---|---|
| `float luxelSize` | 16 | |
| `int padding` | 1 | |
| `std::function<bool(const std::string &)> isUnlit` | sky and liquids | textures without lightmap space |

`std::vector<unsigned char> LightmapLayoutDebugImage(const LightmapLayout &)` draws every chart in its own
color with a luxel checker pattern. `bool IsQuakeUnlitTexture(const std::string &)` is the default
`isUnlit`.
