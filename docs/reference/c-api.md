# C API

Header `src/wrapper.h`, `QLIB_API_VERSION` 2. Usage notes are in the [guide](../guide/c-api.md).

## General

| Function | |
|---|---|
| `uint32_t QLib_GetApiVersion(void)` | |
| `const char *QLib_GetLastError(void)` | last error on the calling thread |

| `QLibResult` | |
|---|---|
| `QLIB_OK` | |
| `QLIB_ERROR_INVALID_ARGUMENT` | null pointer, index out of range, wrong handle |
| `QLIB_ERROR_BUFFER_TOO_SMALL` | |
| `QLIB_ERROR_NOT_READY` | mesh data of a map before `QLibMap_GenerateGeometry` |
| `QLIB_ERROR_LOAD_FAILED` | |
| `QLIB_ERROR_INTERNAL` | |

## Loading

| Function | |
|---|---|
| `QLibMap_GetDefaultOptions(QLibMapOptions *)` | `enableCSG` 1, `convertToOpenGL` 0, `luxelSize` 16, `lightmapPadding` 1 |
| `QLibMap_Load(path, options, &level)` | parse only, `options` may be null |
| `QLibMap_SetTextureSize(level, name, width, height)` | before `GenerateGeometry` |
| `QLibMap_SetFaceType(level, name, QLibSurfaceType)` | before `GenerateGeometry` |
| `QLibMap_GenerateGeometry(level)` | csg, meshes, lightmap uvs |
| `QLibMap_SetExtraLights(level, lights, count)` | before the lightmap is requested |
| `QLibBsp_GetDefaultOptions(QLibBspOptions *)` | `loadTextures` 1, `loadTextureData` 1, `convertToOpenGL` 0 |
| `QLibBsp_Load(path, options, &level)` | |
| `QLibLevel_Destroy(level)` | |

## Entities

| Function | |
|---|---|
| `QLibLevel_GetSolidEntityCount(level)` | entity 0 is the worldspawn |
| `QLibLevel_GetSolidEntityInfo(level, i, &info)` | buffer sizes, bounds, class name |
| `QLibLevel_CopySolidEntityMesh(level, i, vertices, n, indices, n, submeshes, n)` | |
| `QLibLevel_CopyCollisionHulls(level, i, hulls, n, points, n, indices, n)` | maps only |
| `QLibLevel_GetPointEntityCount(level)` | |
| `QLibLevel_GetPointEntityInfo(level, i, &info)` | class name, origin, angle |
| `QLibLevel_GetEntityAttribute(level, solid, i, attribute, &key, &value)` | `solid` 1 for solid entities, 0 for point entities |

## Textures

| Function | |
|---|---|
| `QLibLevel_GetTextureCount(level)` | |
| `QLibLevel_GetTextureInfo(level, i, &info)` | |
| `QLibLevel_CopyTexturePixels(level, i, rgba, bytes)` | bsp only |
| `QLibLevel_CopyTextureEmission(level, i, rgba, bytes)` | bsp only, fails without emission |
| `QLibLevel_GetRequiredWadCount(level)` | |
| `QLibLevel_GetRequiredWad(level, i)` | file name without path |
| `QLibWad_Load(path, flip, &wad)` / `QLibWad_Destroy(wad)` | |
| `QLibWad_GetTextureCount(wad)`, `QLibWad_GetTextureInfo(wad, i, &info)` | |
| `QLibWad_FindTexture(wad, name)` | index, case insensitive, -1 if missing |
| `QLibWad_CopyTexturePixels(wad, i, rgba, bytes)` | |
| `QLibWad_CopyTextureEmission(wad, i, rgba, bytes)` | |

## Lightmap

| Function | |
|---|---|
| `QLibLevel_GetLightmapInfo(level, &info)` | maps are baked on the first call |
| `QLibLevel_CopyLightmap(level, rgba, bytes)` | all layers combined |
| `QLibLevel_CopyLightmapLayer(level, layer, rgba, bytes, &style)` | maps only |
| `QLibLevel_SetLightStyle(level, style, pattern)` | |
| `QLibLevel_UpdateLightStyles(level, time, &changed)` | `changed` 1 if the lightmap has to be copied again |

## Structs

| Struct | |
|---|---|
| `QLibVertex` | `position`, `normal`, `tangent`, `uv`, `lightmapUV` |
| `QLibSubmesh` | vertex and index range, `textureIndex`, `surfaceType` |
| `QLibHull` | `type`, point and index range |
| `QLibTextureInfo` | `name`, `width`, `height`, `isSky`, `hasData`, `hasEmission` |
| `QLibLightmapInfo` | `width`, `height`, `layerCount`, `luxelSize` (0 for bsp) |
| `QLibLight` | same fields as `BakeLight` |
