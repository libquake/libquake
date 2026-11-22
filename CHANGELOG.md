# Changelog

All notable changes to this project will be documented in this file.
For historical releases, see [HISTORY.md](HISTORY.md).

---

## v1.3.0 - Phong Bounce & Luxel Size per Entity (2026-11-22)

### Added

- **`_world_units_per_luxel`** as in the ericw tools: map units per luxel for a brush entity, or for the whole
  map on the worldspawn; `RenderMesh::luxelSize` and `LightmapChart::luxelSize`, the planar layout and the
  baker use each chart's own

### Fixed

- **Phong and bounced light**: smoothed faces took the bounced light of their own patches only, so it jumped at
  every edge between them; they now gather it from the patches around each luxel in space. The angle term fades
  out with the smoothed normal on map faces too, as it already did on meshes

---

## v1.2.1 - Embree on Windows (2026-11-20)

### Added

- **Embree on Windows**: prebuilts for the C runtime dll and for the static runtime (`QLIB_EMBREE_STATIC_RUNTIME`),
  built by the `embree` workflow; MSVC debug builds keep the own bvh

---

## v1.2.0 - Embree, Bounced Light, Ambient Occlusion & Phong (2026-11-20)

### Added

- **Embree** for the baker's rays, linked statically from `third_party/embree` (macOS so far), 3 to 7 times
  faster bakes; `QLIB_EMBREE=OFF` or a platform without a prebuilt keeps the own bvh
- **qviewer**: `--camera "x y z yaw pitch"` sets the start position, C prints the current one
- **Sky domes**: `_sunlight2` and `_sunlight3` with their colors, `BakeLight::Type::SkyDome`
- **`BakeQuality`**: settings that trade bake time for quality and never come from a map: `skySamples`,
  `bounces` and `bounceScale` (replace the map's, -1 keeps it), `bounceSubdivision`;
  `MapBuildOptions::bakeQuality`
- **Bounced light**: `_bounce`, `_bouncescale`, `_bouncecolorscale` and `_bouncestyled` as in the ericw tools,
  for maps and for `BakeSurfaces`; texture colors from `MapBuildOptions::textureColor`,
  `LightmapLayoutOptions::textureColor` or `BakeSurfaces::colors`
- **Supersampling**: `BakeQuality::luxelSamples` 2 or 4, as ericw's `-extra` and `-extra4`
- **Ambient occlusion**: `_dirt`, `_dirtdepth`, `_dirtscale`, `_dirtgain`, `_dirtangle`, `_minlight_dirt`,
  `_sunlight_dirt`, `_sunlight2_dirt` and `_dirt` on lights as in the ericw tools; `BakeQuality::dirt`,
  `dirtScale` and `dirtSamples`; `BakeSurfaces::dirtDepthScales` and `surfaceOffsetScales` for small models
- **qviewer** gives the baker the average color of the wad textures; `--luxel-samples n` for final bakes
- **Phong shading**: `_phong` and `_phong_angle` on brush entities smooth the vertex normals as in the ericw
  tools, the bake lights with them; `LightmapChart::normals`

### Fixed

- CSG: a brush with a `skip` or `clip` face still ends at that face; it cut away the faces of other brushes
  beyond it, which left holes (qbjr_rabbit: 2.3 % of the area missing, now none)
- CSG across the world's entities as in the Quake compilers: worldspawn and `func_group` cut each other,
  `func_detail` cuts other details and the world; faces of one detail inside another were kept and baked black
  (qbj3_radiatoryang)
- CSG: faces that collapse to a line, the top and bottom of very thin wedges, are dropped; their old indices
  pointed past their corners, gave stray triangles and crashed the lightmap layout (qbj3_radiatoryang)

---

*Previous versions: [v1.1.0](HISTORY.md#v110---c-api-light-baker--bsp2-2026-11-18) | [v1.0.3](HISTORY.md#v103---lightmap-generation--non-solid-brush-export-2026-01-26) | [v1.0.2](HISTORY.md#v102---enhanced-api--uv-fixes-2026-01-26) | [v1.0.1](HISTORY.md#v101---bug-fixes-2026-01-20) | [v1.0.0](HISTORY.md#v100---initial-release-2026-01-15)*
