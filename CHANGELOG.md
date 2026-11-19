# Changelog

All notable changes to this project will be documented in this file.
For historical releases, see [HISTORY.md](HISTORY.md).

---

## Unreleased

### Added

- **Embree** for the baker's rays, linked statically from `third_party/embree` (macOS so far), 3 to 7 times
  faster bakes; `QLIB_EMBREE=OFF` or a platform without a prebuilt keeps the own bvh
- **qviewer**: `--camera "x y z yaw pitch"` sets the start position, C prints the current one
- **Sky domes**: `_sunlight2` and `_sunlight3` with their colors, `BakeLight::Type::SkyDome`
- **`BakeQuality`**: settings that trade bake time for quality and never come from a map, `skySamples` for now;
  `MapBuildOptions::bakeQuality`

---

## v1.1.0 - C++ API, Light Baker & BSP2 (2026-11-18)

A rewrite of the public API. Maps and bsp files load into one structure, `quakelib::Level`, and the C API
follows it. Code written against v1.0.x has to be ported, see the [docs](https://libquake.github.io/).

### Added

- **C++ API**: `LoadBsp`, `ParseMap` / `BuildMap` and `quakelib::Level` with entities, meshes per texture,
  collision hulls, textures and lightmaps
- **Light baker** for `.map` files: point, spot and sun lights with shadows, Quake's light keys and
  worldspawn settings, extra lights and occluders, light styles and switchable lights
- **Surface baker**: `BakeLightmap(BakeSurfaces, ...)` lights triangles of any shape placed by their own
  uvs, e.g. models next to brushes; `BakeOptions::ambient`
- **Lightmaps split by light style** for maps and bsp files (with `.lit`), `LightStyleAnimator` composes them
- **Lightmap uvs**: planar (Quake's luxel grid) or xatlas unwrap, `LightmapLayout` to bake with your own
  lights and occluders
- **BSP2** bsp files next to version 29
- **Brush entity pivots**: origin brush, `_pivot` key or `MapBuildOptions::pivot` (`MapOrigin`, `Center`,
  `BottomCenter`, `BottomCorner`)
- **`MapBuildOptions::mergeGroups`**: one geometry per TrenchBroom group
- **Collision hulls** per brush, `Solid`, `Clip` or `Liquid`; liquid hulls carry their texture name
- **Textures**: classification by name (`ClassifyTexture`: sky, `{` alpha, `*` liquid), fullbright emission
  maps, sky split into back and cloud layer
- **C API v3** (`QLIB_API_VERSION` 3, `QLib_GetApiVersion`): `QLibLevel_*` for both file types, texture
  layers, lightmap per style, light style updates, `QLib_GetLastError`
- **qviewer**: fly camera, bloom, sky layers, animated light styles
- Builds with MSVC

### Changed

- **C API**: the v1.0.x functions (`QLibMap_ExportAll`, `QLibMap_GenerateLightmaps*`,
  `QLibBsp_GetEntityMesh`, ...) are replaced by the `QLibLevel_*` functions; data is copied into buffers
  the caller allocates
- Liquid textures may start with `#` where the name comes from a file (Windows does not allow `*`)
- Liquid brushes no longer hide faces under them in CSG; the liquid is still cut by solid brushes
- WAD lookup is case insensitive, an exact match wins
- Palette index 255 is transparent only on `{` textures
- Universal macOS binaries only when libquake is built on its own; as a subdirectory the parent project
  picks the architectures
- Math with HandmadeMath instead of tue

### Fixed

- qviewer crash on exit

---

*Previous versions: [v1.0.3](HISTORY.md#v103---lightmap-generation--non-solid-brush-export-2026-01-26) | [v1.0.2](HISTORY.md#v102---enhanced-api--uv-fixes-2026-01-26) | [v1.0.1](HISTORY.md#v101---bug-fixes-2026-01-20) | [v1.0.0](HISTORY.md#v100---initial-release-2026-01-15)*
