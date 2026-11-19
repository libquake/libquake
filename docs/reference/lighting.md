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

```cpp
BakedLightmap BakeLightmap(const BakeSurfaces &surfaces, const BakeOccluders &occluders,
                           const std::vector<BakeLight> &lights, const BakeOptions &options = {});
```

Bakes triangles of any shape, e.g. models next to brushes, placed in the atlas by their own uvs (an unwrap,
or the uvs of an earlier layout). Every texel is found by rasterizing the triangles; texels at an edge sample
the closest point on the triangle. On smooth triangles (normals that differ from the face normal) the angle
independent share of the light fades out towards the shadow line instead of dropping at 90°.

| `BakeSurfaces` | Default | |
|---|---|---|
| `int width`, `height` | | atlas size |
| `std::vector<Vec3> positions` | | 3 per triangle |
| `std::vector<Vec3> normals` | | 3 per triangle, interpolated across it |
| `std::vector<Vec2> uvs` | | 3 per triangle, in texels: texel (i, j) covers [i, i + 1) × [j, j + 1) |
| `int padding` | 1 | texels filled around the triangles |
| `std::vector<Vec3> colors` | | one per triangle, average texture color for bounced light, may be empty (gray) |

| `BakeLight` | Default | |
|---|---|---|
| `Type type` | `Point` | `Point`, `Spot`, `Sun` or `SkyDome` |
| `Falloff falloff` | `Linear` | Quake's `delay` key: `Linear`, `Inverse`, `InverseSquare`, `None`, `LocalMinlight`, `InverseSquareB` |
| `Vec3 position` | | |
| `Vec3 direction` | `{0, 0, -1}` | spot, sun and sky dome: the way the light shines |
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
| `Vec3 ambient` | `{0, 0, 0}` | added to the static light before `minLight`, in lightmap units (128 is normal) |
| `float surfaceOffset` | 1 | distance of the samples from the surface |
| `int bounces` | 0 | `_bounce`, times light bounces off surfaces |
| `float bounceScale` | 1 | `_bouncescale` |
| `float bounceColorScale` | 0 | `_bouncecolorscale`, 0 bounces gray, 1 the textures' color |
| `bool bounceStyled` | `false` | `_bouncestyled`, animated and switchable styles bounce too |
| `int threads` | 0 | 0 uses all hardware threads |
| `BakeQuality quality` | | never read from a map |

| `BakeQuality` | Default | |
|---|---|---|
| `int skySamples` | 64 | suns per sky dome, rounded to its rings (ericw's `-sunsamples`) |
| `int bounces` | -1 | replaces the map's `bounces`, -1 keeps them; 1 gives old maps bounced light |
| `float bounceScale` | -1 | replaces the map's `bounceScale`, negative keeps it; lower it when bounced light brightens an old map too much |
| `float bounceSubdivision` | 64 | map units per patch that bounces light, larger bake faster |

A `SkyDome` is light from the whole sky, `_sunlight2` (from above) and `_sunlight3` (from below) in the ericw
tools: suns on rings over a half sphere around `direction`, together as bright as `intensity`. Like a sun it
only reaches surfaces that see the sky.

Bounced light follows the ericw tools too: the light on a patch of `bounceSubdivision` units, times its
reflectance and area, reaches other patches with cos · cos / max(distance, 128)², scaled by `bounceScale` / 2.
The reflectance is gray (127 / 255) mixed with the average texture color by `bounceColorScale`. Patches
receive the bounced light and spread it over their luxels, so it costs far fewer rays than direct light.
Far away patches facing the same way send together, as one.

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
| `std::vector<Vec3> colors` | one per triangle, average texture color for bounced light, empty without `textureColor` |
| `Vec3 LuxelPosition(float i, float j, float luxelSize) const` | world position of a luxel |

| `LightmapLayoutOptions` | Default | |
|---|---|---|
| `float luxelSize` | 16 | |
| `int padding` | 1 | |
| `std::function<bool(const std::string &)> isUnlit` | sky and liquids | textures without lightmap space |
| `std::function<Vec3(const std::string &)> textureColor` | none | average texture color (0..1), fills `LightmapChart::colors` |

`std::vector<unsigned char> LightmapLayoutDebugImage(const LightmapLayout &)` draws every chart in its own
color with a luxel checker pattern. `bool IsQuakeUnlitTexture(const std::string &)` is the default
`isUnlit`.
