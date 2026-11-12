# C API

Header `src/wrapper.h`, `QLIB_API_VERSION` 3. Usage notes are in the [guide](../guide/c-api.md).

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
| `QLibLevel_GetSolidEntityCount(level)` | entities with geometry in file order, entity 0 is the worldspawn |
| `QLibLevel_GetSolidEntityInfo(level, i, &info)` | buffer sizes, bounds, class name |
| `QLibLevel_CopySolidEntityMesh(level, i, vertices, n, indices, n, submeshes, n)` | |
| `QLibLevel_CopyCollisionHulls(level, i, hulls, n, points, n, indices, n)` | maps only |
| `QLibLevel_GetPointEntityCount(level)` | all other entities in file order |
| `QLibLevel_GetPointEntityInfo(level, i, &info)` | class name, origin (converted with `convertToOpenGL`), `angle` key |
| `QLibLevel_GetEntityAttribute(level, solid, i, attribute, &key, &value)` | `solid` 1 for solid entities, 0 for point entities. Attribute 0 is the classname, then all other keys as written, sorted by key |

## Textures

| Function | |
|---|---|
| `QLibLevel_GetTextureCount(level)` | |
| `QLibLevel_GetTextureInfo(level, i, &info)` | |
| `QLibLevel_CopyTextureLayer(level, i, layer, rgba, bytes)` | bsp only, fails if the texture does not have the layer |
| `QLibLevel_GetRequiredWadCount(level)` | |
| `QLibLevel_GetRequiredWad(level, i)` | file name without path |
| `QLibWad_Load(path, flip, &wad)` / `QLibWad_Destroy(wad)` | |
| `QLibWad_GetTextureCount(wad)`, `QLibWad_GetTextureInfo(wad, i, &info)` | |
| `QLibWad_FindTexture(wad, name)` | index, case insensitive, -1 if missing |
| `QLibWad_CopyTextureLayer(wad, i, layer, rgba, bytes)` | rows flipped when loaded with `flip` |

| `QLibTextureLayer` | |
|---|---|
| `QLIB_TEXTURE_LAYER_ALBEDO` | the texture, the back layer of skies. `{` textures: index 255 transparent |
| `QLIB_TEXTURE_LAYER_EMISSION` | black except the fullbright pixels |
| `QLIB_TEXTURE_LAYER_SKY_FRONT` | cloud layer of skies, index 0 transparent |

| `QLibTextureFlags` | |
|---|---|
| `QLIB_TEXTURE_SKY` | name starts with `sky` |
| `QLIB_TEXTURE_ALPHA` | name starts with `{`, draw with alpha test |
| `QLIB_TEXTURE_LIQUID` | name starts with `*` |
| `QLIB_TEXTURE_HAS_DATA` | albedo layer available |
| `QLIB_TEXTURE_HAS_EMISSION` | emission layer available |
| `QLIB_TEXTURE_HAS_SKY_FRONT` | sky front layer available |

The name flags are set for maps too, the layer flags only where pixels exist. Sky textures report the size
of one layer (128x128 for a 256x128 sky).

## Lightmap

| Function | |
|---|---|
| `QLibLevel_GetLightmapInfo(level, &info)` | maps are baked on the first call |
| `QLibLevel_CopyLightmap(level, rgba, bytes)` | all layers combined |
| `QLibLevel_CopyLightmapLayer(level, layer, rgba, bytes, &style)` | one layer per light style, maps and bsp |
| `QLibLevel_SetLightStyle(level, style, pattern)` | |
| `QLibLevel_UpdateLightStyles(level, time, &changed)` | `changed` 1 if the lightmap has to be copied again |

## Structs

| Struct | |
|---|---|
| `QLibVertex` | `position`, `normal`, `tangent`, `uv`, `lightmapUV` |
| `QLibSubmesh` | vertex and index range, `textureIndex`, `surfaceType` |
| `QLibHull` | `type`, point and index range |
| `QLibTextureInfo` | `name`, `width`, `height`, `flags` (`QLibTextureFlags`) |
| `QLibLightmapInfo` | `width`, `height`, `layerCount`, `luxelSize` (0 for bsp) |
| `QLibLight` | same fields as `BakeLight` |
