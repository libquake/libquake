# C API

For bindings (C#, Python, Godot GDExtension, ...) the library exports a plain C interface. The header is
`src/wrapper.h`; copy it next to your binding or add `src/` to the include path.

All functions return a `QLibResult`. On failure `QLib_GetLastError()` has the message for the calling
thread. Data is copied into buffers you allocate: ask for the sizes first, then copy.

```c
--8<-- "examples/c_api.c:load"
```

Check `QLib_GetApiVersion()` against the `QLIB_API_VERSION` your binding was written for. It changes on
every incompatible change.

## Maps

`QLibMap_Load` only parses. Set texture sizes and face types, then call `QLibMap_GenerateGeometry`:

```c
QLibMap_SetTextureSize(level, "city4_6", 64, 64);
QLibMap_SetFaceType(level, "invisible_wall", QLIB_SURFACE_CLIP);
QLibMap_GenerateGeometry(level);
```

The lightmap of a map is baked on the first `QLibLevel_GetLightmapInfo` call. Extra lights
(`QLibMap_SetExtraLights`) have to be set before.

## Meshes

Solid entities are indexed from 0, entity 0 is the worldspawn. Each one has one vertex and index buffer
with a submesh per texture:

```c
--8<-- "examples/c_api.c:mesh"
```

Submesh indices already include `vertexOffset`, so a submesh can be drawn straight from the shared buffers.

`QLibLevel_CopyCollisionHulls` returns one convex hull per brush (maps only), with indices relative to the
hull's `pointOffset`.

## Lightmap

```c
--8<-- "examples/c_api.c:lightmap"
```

`QLibLevel_CopyLightmap` returns all layers combined with the current light style values. Call
`QLibLevel_UpdateLightStyles` every frame and copy again when `changed` is set.
`QLibLevel_CopyLightmapLayer` gives single layers of a map for engines that combine them in a shader.

## Textures

`QLibLevel_GetTextureInfo` and `QLibLevel_CopyTexturePixels` read textures embedded in a bsp. For maps, load
the wads named by `QLibLevel_GetRequiredWad` with `QLibWad_Load` and look the textures up with
`QLibWad_FindTexture`. Both have a `Copy*TextureEmission` function for textures with `hasEmission`.

## Lifetime

Every `*_Load` has a matching `*_Destroy`. Strings returned by the library (`className`, texture names,
attributes) stay valid until the object they came from is destroyed.

A level must not be used from two threads at once. Different levels are independent.
