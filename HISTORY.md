# Release History

This file contains archived release notes for historical versions.

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

## v1.0.3 - Lightmap Generation & Non-Solid Brush Export (2026-01-26)

### Added

- **Lightmap Generation API**: Complete lightmap atlas generation system with three new wrapper functions:
  - `QLibMap_GenerateLightmaps()` - Generates lightmap atlas and updates vertex UVs to normalized (0-1) atlas coordinates
  - `QLibMap_CalculateLighting()` - Bakes diffuse lighting from point lights with distance attenuation
  - `QLibMap_GenerateLightmapsAuto()` - Convenience function that automatically extracts light entities from MAP file and bakes lighting
  - `QLibMap_GetLightmapData()` - Exports RGBA lightmap atlas texture
  - `QLibMap_FreeLightmapData()` - Cleanup function
- **QLibMapLight Structure**: Point light definition with position, radius, and RGB color for lightmap baking
- **QLibMapLightmapData Structure**: Lightmap atlas data with width, height, dataSize, and RGBA pixel data
- **Comprehensive Lightmap Documentation**: Complete API reference with usage examples, shader integration guide, and light extraction patterns

### Fixed

- **CLIP/SKIP/NODRAW Brush Export**: Non-solid brushes (CLIP, SKIP, NODRAW, trigger) now properly export without CSG operations. Previously these brushes were being filtered out during CSG, resulting in no mesh output. The CSG system now skips non-solid brushes entirely, allowing them to export with their original geometry.

### Changed

- **CSG Optimization for Non-Solid Brushes**: Added `IsNonSolidBrush()` method to Brush class with cached flag (`m_isNonSolid`) set during geometry generation. Non-solid brushes skip CSG union operations and don't participate in clipping other brushes.
- **Lightmap UV Workflow**: Vertex `lightmapUV` field now populated with normalized atlas coordinates (0-1 range) after calling `QLibMap_GenerateLightmaps()`. Previously contained world-space coordinates.

### Technical Notes

**Lightmap System Architecture**:
- Face packing uses bin-packing algorithm to fit face lightmap regions into atlas
- Luxel size determines lightmap resolution (typical: 16.0 world units per texel)
- Lighting calculation uses simple diffuse model: `attenuation = (1 - distance/radius)²`
- No shadow casting (future enhancement)
- Atlas format is RGBA (4 bytes per pixel)
- Without lighting calculation, returns debug checkerboard pattern

**Non-Solid Brush Handling**:
- All faces in a brush must have the same type (solid or non-solid, never mixed)
- Non-solid status cached during `calculateAABB()` by checking first face type
- CLIP brushes export for collision-only geometry
- SKIP brushes can be used for optimization hints
- NODRAW brushes useful for sealing maps without visible geometry

---

## v1.0.2 - Enhanced API & UV Fixes (2026-01-26)

### Added

- **`QLibMap_GetTextureNames()` API**: New function to retrieve all texture names used in a MAP file, enabling flexible texture loading from any source (disk, memory, WAD, procedural) without being limited to WAD-only workflow.

### Fixed

- **Valve UV Format Parsing**: Fixed mapVersion detection bug that prevented Valve format UV coordinates from being parsed correctly. The parser now properly identifies and handles Valve 220 format MAP files.
- **Bracket Removal in MAP Parser**: Implemented proper bracket removal from MAP file texture definition lines before parsing, matching the behavior of the original libqmap implementation.

### Changed

- **Decoupled Texture Registration**: The texture dimension registration workflow is now fully decoupled from WAD files. Users can call `QLibMap_GetTextureNames()` to get texture requirements, then `QLibMap_RegisterTextureSize()` from any source, before finally calling `QLibMap_GenerateGeometry()`.

### Technical Notes

**UV Calculation Fix**: The root cause of UV coordinates calculating as infinity was that mapVersion wasn't being parsed from MAP files, causing the parser to default to standard format when the file used Valve format. This led to stream misalignment and zero values for scale parameters, resulting in division by zero during UV calculation.

---

## v1.0.1 - Bug Fixes (2026-01-20)

### Fixed

- **WAD Wrapper RGBA Conversion**: Fixed incorrect byte size calculation when converting texture data from internal `std::vector<color>` to C API byte array. Was setting `dataSize` to pixel count instead of `pixel_count * 4`, causing truncated texture data export.
- **Memory Leak in WAD Loading**: Fixed leak in `QuakeWad::GetTexture()` where dynamically allocated texture from `FromBuffer()` was copied but never freed.

### Added

- **C Wrapper API Documentation**: Comprehensive documentation with struct memory layouts and code examples for WAD, BSP, and MAP APIs.
- **Unit Tests**: Added extensive test coverage for WAD loading and wrapper API validation.

### Technical Notes

**Why `textureName` in submesh?** Including the texture name directly in each submesh makes it self-contained for FFI consumers—no need to maintain separate lookup tables. It also handles missing textures gracefully (when `textureID` is -1) and provides better error messages during debugging.

---

## v1.0.0 - Initial Release (2026-01-15)

### Features

- **BSP File Support**: Load and parse Quake BSP files with comprehensive metadata
  - Texture loading with optional texture data
  - Lightmap support with UV coordinates
  - Entity parsing
  - Coordinate system conversion (Quake to OpenGL)

- **MAP File Support**: Load and process Quake MAP source files
  - Standard and Valve 220 format support
  - CSG (Constructive Solid Geometry) operations
  - Texture coordinate calculation
  - Brush clipping and geometry generation

- **WAD File Support**: Load and extract textures from WAD archives
  - Texture metadata (width, height, type)
  - Mipmap support
  - Palette data

- **Unified C-Style Wrapper API**: Easy integration with C# and Unity
  - Batch-export functions for efficient data transfer
  - Contiguous memory layouts
  - QLib-prefixed naming convention

- **Coordinate System Conversion**: Seamless conversion between Quake and OpenGL coordinate systems

- **Documentation**: Comprehensive documentation with code examples
  - API documentation for all classes
  - Format specifications
  - Usage examples
  - Lightmapping algorithms explained with code

### Technical Details

- Cross-platform support (Windows, macOS, Linux)
- Universal binary for macOS (arm64 + x86_64)
- Modern C++20 implementation
- CMake build system
- Includes qviewer tool for visualization
