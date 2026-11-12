#pragma once

/*
 * C interface of libquake, used through P/Invoke (Unity / C#) and other FFIs.
 *
 * Rules:
 * - every function returning QLibResult catches all exceptions, details come from QLib_GetLastError()
 * - data is copied into buffers owned by the caller, query the sizes first with the *Info functions.
 *   nothing allocated by the library has to be freed by the caller, except the handles (Destroy).
 * - strings returned as const char* belong to the handle and stay valid until it is destroyed.
 * - all structs are plain data (blittable), the layout is the same on all 64 bit platforms.
 * - a handle must not be used from two threads at the same time.
 */

#include <stdint.h>

#ifdef __cplusplus
#define QLIB_EXTERNC extern "C"
#else
#define QLIB_EXTERNC
#endif

#if defined(_WIN32)
#define API_EXPORT QLIB_EXTERNC __declspec(dllexport)
#else
#define API_EXPORT QLIB_EXTERNC __attribute__((visibility("default")))
#endif

/// bumped on every incompatible change, bindings should compare it with the version they were written for
#define QLIB_API_VERSION 2

typedef enum QLibResult {
  QLIB_OK = 0,
  QLIB_ERROR_INVALID_ARGUMENT = 1, ///< null pointer, index out of range, wrong handle type
  QLIB_ERROR_BUFFER_TOO_SMALL = 2,
  QLIB_ERROR_NOT_READY = 3, ///< e.g. mesh data requested before QLibMap_GenerateGeometry
  QLIB_ERROR_LOAD_FAILED = 4,
  QLIB_ERROR_INTERNAL = 5, ///< exception inside the library, see QLib_GetLastError
} QLibResult;

typedef struct QLibVec2 {
  float x, y;
} QLibVec2;

typedef struct QLibVec3 {
  float x, y, z;
} QLibVec3;

typedef struct QLibVec4 {
  float x, y, z, w;
} QLibVec4;

typedef struct QLibVertex {
  QLibVec3 position;
  QLibVec3 normal;
  QLibVec4 tangent; ///< w: bitangent sign, bitangent = cross(normal, tangent.xyz) * w
  QLibVec2 uv;
  QLibVec2 lightmapUV; ///< v = 0 is the first row of the lightmap data
} QLibVertex;

/// same values as quakelib::SurfaceType
typedef enum QLibSurfaceType {
  QLIB_SURFACE_SOLID = 0,
  QLIB_SURFACE_CLIP = 1,
  QLIB_SURFACE_SKIP = 2,
  QLIB_SURFACE_NODRAW = 3,
} QLibSurfaceType;

typedef struct QLibSubmesh {
  uint32_t vertexOffset;
  uint32_t vertexCount;
  uint32_t indexOffset; ///< indices are entity wide, they already include vertexOffset
  uint32_t indexCount;
  int32_t textureIndex; ///< see QLibLevel_GetTextureInfo, -1 if unknown
  uint32_t surfaceType; ///< QLibSurfaceType
} QLibSubmesh;

typedef struct QLibSolidEntityInfo {
  const char *className;
  QLibVec3 boundsMin;
  QLibVec3 boundsMax;
  uint32_t vertexCount;
  uint32_t indexCount;
  uint32_t submeshCount;
  uint32_t attributeCount;
  uint32_t hullCount; ///< collision hulls, MAP only
  uint32_t hullPointCount;
  uint32_t hullIndexCount;
  uint32_t reserved;
} QLibSolidEntityInfo;

typedef struct QLibPointEntityInfo {
  const char *className;
  QLibVec3 origin;
  float angle;
  uint32_t attributeCount;
  uint32_t reserved;
} QLibPointEntityInfo;

typedef enum QLibHullType {
  QLIB_HULL_SOLID = 0,
  QLIB_HULL_CLIP = 1,   ///< invisible, blocks players in Quake
  QLIB_HULL_LIQUID = 2, ///< water / slime / lava volume
} QLibHullType;

/// one convex hull, a range in the point and index arrays of QLibLevel_CopyCollisionHulls
typedef struct QLibHull {
  uint32_t type; ///< QLibHullType
  uint32_t pointOffset;
  uint32_t pointCount;
  uint32_t indexOffset; ///< indices are relative to pointOffset
  uint32_t indexCount;
} QLibHull;

typedef struct QLibLightmapInfo {
  uint32_t width;
  uint32_t height;
  uint32_t layerCount; ///< one layer per light style, layer 0 is style 0
  float luxelSize;     ///< map units per luxel, 0 for BSP files
} QLibLightmapInfo;

typedef enum QLibLightType {
  QLIB_LIGHT_POINT = 0,
  QLIB_LIGHT_SPOT = 1,
  QLIB_LIGHT_SUN = 2,
} QLibLightType;

/// an additional light for the baker, in map coordinates, same meaning as quakelib::BakeLight
typedef struct QLibLight {
  uint32_t type;    ///< QLibLightType
  uint32_t falloff; ///< 0 linear, 1 inverse, 2 inverse square, 3 none, 4 local minlight, 5 inverse square B
  QLibVec3 position;
  QLibVec3 direction;
  QLibVec3 color;  ///< 0..1
  float intensity; ///< Quake "light" value, 300 is the default
  float distanceScale;
  float coneAngle;
  float softAngle;
  int32_t style;
  uint32_t castShadows;
} QLibLight;

typedef struct QLibMapOptions {
  uint32_t enableCSG;       ///< remove hidden faces between brushes, default 1
  uint32_t convertToOpenGL; ///< (x, y, z) -> (x, z, -y), default 0
  float luxelSize;          ///< map units per lightmap texel, default 16
  uint32_t lightmapPadding; ///< texels between lightmap charts, default 1
} QLibMapOptions;

typedef struct QLibBspOptions {
  uint32_t loadTextures;    ///< default 1
  uint32_t loadTextureData; ///< default 1
  uint32_t convertToOpenGL; ///< default 0
  uint32_t reserved;
} QLibBspOptions;

typedef struct QLibTextureInfo {
  const char *name;
  uint32_t width;
  uint32_t height;
  uint32_t isSky;
  uint32_t hasData;     ///< 0 if the texture has no pixels (e.g. BSP compiled with -notex)
  uint32_t hasEmission; ///< 1 if the texture has fullbright pixels, see Copy*TextureEmission
  uint32_t reserved;
} QLibTextureInfo;

/// a loaded MAP or BSP file
typedef struct QLibLevel QLibLevel;
typedef struct QLibWad QLibWad;

// ============================================================================
// General
// ============================================================================

API_EXPORT uint32_t QLib_GetApiVersion(void);

/// message of the last error on the calling thread, empty if there was none
API_EXPORT const char *QLib_GetLastError(void);

// ============================================================================
// MAP and BSP files
// ============================================================================

API_EXPORT void QLibMap_GetDefaultOptions(QLibMapOptions *options);

/// parses a .map file, geometry is built by QLibMap_GenerateGeometry. options may be null.
API_EXPORT QLibResult QLibMap_Load(const char *path, const QLibMapOptions *options, QLibLevel **outLevel);

/// texture size for the texture uvs, names are case insensitive. call before QLibMap_GenerateGeometry.
API_EXPORT QLibResult QLibMap_SetTextureSize(QLibLevel *level, const char *textureName, uint32_t width,
                                             uint32_t height);

/// marks faces with this texture (case insensitive), call before QLibMap_GenerateGeometry
API_EXPORT QLibResult QLibMap_SetFaceType(QLibLevel *level, const char *textureName, uint32_t surfaceType);

/// csg, meshes, lightmap uvs. MAP files need this before any mesh data can be read.
API_EXPORT QLibResult QLibMap_GenerateGeometry(QLibLevel *level);

/// lights baked in addition to the map's own lights, call before the lightmap is requested
API_EXPORT QLibResult QLibMap_SetExtraLights(QLibLevel *level, const QLibLight *lights, uint32_t count);

API_EXPORT void QLibBsp_GetDefaultOptions(QLibBspOptions *options);
API_EXPORT QLibResult QLibBsp_Load(const char *path, const QLibBspOptions *options, QLibLevel **outLevel);

API_EXPORT void QLibLevel_Destroy(QLibLevel *level);

API_EXPORT uint32_t QLibLevel_GetTextureCount(QLibLevel *level);
API_EXPORT QLibResult QLibLevel_GetTextureInfo(QLibLevel *level, uint32_t index, QLibTextureInfo *outInfo);
/// RGBA pixels of a texture embedded in a BSP, width * height * 4 bytes
API_EXPORT QLibResult QLibLevel_CopyTexturePixels(QLibLevel *level, uint32_t index, uint8_t *rgba,
                                                  uint32_t byteCount);

/// RGBA emission map of a BSP texture: black except the fullbright pixels (palette 224..254), same size and
/// layout as the pixels. Fails if the texture has no emission.
API_EXPORT QLibResult QLibLevel_CopyTextureEmission(QLibLevel *level, uint32_t index, uint8_t *rgba,
                                                    uint32_t byteCount);

/// names of the wad files a MAP file asks for (worldspawn "wad" key, without path)
API_EXPORT uint32_t QLibLevel_GetRequiredWadCount(QLibLevel *level);
API_EXPORT const char *QLibLevel_GetRequiredWad(QLibLevel *level, uint32_t index);

API_EXPORT uint32_t QLibLevel_GetSolidEntityCount(QLibLevel *level);
API_EXPORT QLibResult QLibLevel_GetSolidEntityInfo(QLibLevel *level, uint32_t entity,
                                                   QLibSolidEntityInfo *outInfo);
API_EXPORT QLibResult QLibLevel_CopySolidEntityMesh(QLibLevel *level, uint32_t entity, QLibVertex *vertices,
                                                    uint32_t vertexCapacity, uint32_t *indices,
                                                    uint32_t indexCapacity, QLibSubmesh *submeshes,
                                                    uint32_t submeshCapacity);
/// convex hull per brush (MAP files), points in map coordinates
API_EXPORT QLibResult QLibLevel_CopyCollisionHulls(QLibLevel *level, uint32_t entity, QLibHull *hulls,
                                                   uint32_t hullCapacity, QLibVec3 *points,
                                                   uint32_t pointCapacity, uint32_t *indices,
                                                   uint32_t indexCapacity);

API_EXPORT uint32_t QLibLevel_GetPointEntityCount(QLibLevel *level);
API_EXPORT QLibResult QLibLevel_GetPointEntityInfo(QLibLevel *level, uint32_t entity,
                                                   QLibPointEntityInfo *outInfo);

/// key / value of an entity attribute, solid: 1 for solid entities, 0 for point entities
API_EXPORT QLibResult QLibLevel_GetEntityAttribute(QLibLevel *level, uint32_t solid, uint32_t entity,
                                                   uint32_t attribute, const char **outKey,
                                                   const char **outValue);

/// BSP: the stored lightmap. MAP: bakes it on the first call (map lights + extra lights), can take a moment.
API_EXPORT QLibResult QLibLevel_GetLightmapInfo(QLibLevel *level, QLibLightmapInfo *outInfo);
/// RGBA, width * height * 4 bytes, all layers combined with the current light style values
API_EXPORT QLibResult QLibLevel_CopyLightmap(QLibLevel *level, uint8_t *rgba, uint32_t byteCount);
/// RGBA of one light style layer (MAP files), the light style number goes to outStyle
API_EXPORT QLibResult QLibLevel_CopyLightmapLayer(QLibLevel *level, uint32_t layer, uint8_t *rgba,
                                                  uint32_t byteCount, int32_t *outStyle);
/// light style pattern, 'a' dark, 'm' normal, 'z' double. style 32 and up are switchable lights.
API_EXPORT QLibResult QLibLevel_SetLightStyle(QLibLevel *level, int32_t style, const char *pattern);
/// evaluates the patterns at a time in seconds, outChanged is 1 if the lightmap has to be copied again
API_EXPORT QLibResult QLibLevel_UpdateLightStyles(QLibLevel *level, double time, uint32_t *outChanged);

// ============================================================================
// WAD files
// ============================================================================

API_EXPORT QLibResult QLibWad_Load(const char *path, uint32_t flipHorizontal, QLibWad **outWad);
API_EXPORT void QLibWad_Destroy(QLibWad *wad);
API_EXPORT uint32_t QLibWad_GetTextureCount(QLibWad *wad);
API_EXPORT QLibResult QLibWad_GetTextureInfo(QLibWad *wad, uint32_t index, QLibTextureInfo *outInfo);
/// index of a texture by name (case insensitive), -1 if not found
API_EXPORT int32_t QLibWad_FindTexture(QLibWad *wad, const char *name);
API_EXPORT QLibResult QLibWad_CopyTexturePixels(QLibWad *wad, uint32_t index, uint8_t *rgba,
                                                uint32_t byteCount);
/// RGBA emission map, black except the fullbright pixels, same layout as CopyTexturePixels
API_EXPORT QLibResult QLibWad_CopyTextureEmission(QLibWad *wad, uint32_t index, uint8_t *rgba,
                                                  uint32_t byteCount);
