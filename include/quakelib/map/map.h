//
// Created by tgoeh on 10.11.2023.
//
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "brush.h"
#include "entities.h"
#include "map_file.h"
#include "types.h"
#include <quakelib/config.h>

namespace quakelib::map {
  /**
   * @brief Configuration options for loading MAP files.
   *
   * Extends the base Config with MAP-specific options for controlling
   * geometry processing and CSG operations.
   */
  struct QMapConfig : public Config {
    /**
     * @brief Enable Constructive Solid Geometry (CSG) operations.
     *
     * When enabled, performs brush-to-brush clipping to handle intersecting
     * geometry properly. This creates clean intersections and prevents
     * overlapping faces, but increases processing time.
     *
     * Disable for faster preview rendering at the cost of visual artifacts
     * where brushes intersect.
     */
    bool csg = true;
  };

  /**
   * @brief Callback type for gathering polygons.
   * @param faces Vector of faces collected.
   * @param textureID The texture ID associated with these faces (or -1/generic).
   */
  using polygonGatherCb = std::function<void(std::vector<FacePtr>, int)>;

  /**
   * @brief Callback type for retrieving texture dimensions.
   * @param textureName The name of the texture.
   * @return The bounding dimensions of the texture.
   */
  using getTextureBoundsCb = std::function<textureBounds(const char *textureName)>;

  /**
   * @brief High-level class for loading and processing Quake .map files.
   *
   * QMap handles loading and processing of Quake source map files (.map).
   * It parses brush and entity definitions, performs CSG operations,
   * generates triangulated meshes, and provides access to all level data.
   *
   * Supports both Standard Quake (version 100) and Valve 220 map formats.
   *
   * ## Features
   * - Brush geometry construction from plane equations
   * - CSG (Constructive Solid Geometry) for intersecting brushes
   * - Automatic mesh triangulation
   * - T-junction fixing for proper rendering
   * - Texture coordinate calculation
   * - Entity management (solid and point entities)
   * - Optional coordinate system conversion
   *
   * @see QMapConfig for configuration options
   * @see QMapProvider for the IMapProvider interface implementation
   */
  class QMap {
  public:
    /**
     * @brief Default constructor with default configuration.
     */
    QMap() = default;

    /**
     * @brief Construct with custom configuration.
     * @param cfg Configuration options for loading and processing.
     */
    QMap(QMapConfig cfg) : m_config(cfg) {};

    /**
     * @brief Destructor.
     */
    ~QMap() = default;

    /**
     * @brief Loads a map from a file on disk.
     * @param filename Path to the map file.
     * @param getTextureBounds Optional callback to retrieve texture sizes for UV calculations.
     */
    void LoadFile(const std::string &filename, getTextureBoundsCb getTextureBounds = nullptr);

    /**
     * @brief Loads a map from a string buffer.
     * @param buffer Null-terminated string containing the map data.
     * @param getTextureBounds Callback to retrieve texture sizes.
     */
    void LoadBuffer(const char *buffer, getTextureBoundsCb getTextureBounds);

    void LoadDocument(const MapDocument &doc);

    /**
     * @brief Registers a callback for texture bounds retrieval.
     *
     * This is useful if texture data becomes available after loading but before geometry generation.
     * @param getTextureBounds The callback function.
     */
    void RegisterTextureBounds(getTextureBoundsCb getTextureBounds);

    /**
     * @brief Sets the configuration for the map.
     *
     * Updates the configuration used for geometry processing. Should be called
     * before GenerateGeometry() to take effect.
     *
     * @param cfg The new configuration to apply.
     */
    void SetConfig(const QMapConfig &cfg) { m_config = cfg; }

    const QMapConfig &Config() const { return m_config; }

    /**
     * @brief Generates renderable geometry from brush definitions.
     *
     * Performs the following operations based on configuration:
     * - Builds brush geometry from planes
     * - Applies CSG operations if enabled (clips intersecting brushes)
     * - Triangulates all faces
     * - Fixes T-junctions
     * - Welds vertices
     * - Converts coordinates if requested
     *
     * Must be called after loading and before accessing geometry.
     *
     * @note This can be time-consuming for complex maps with CSG enabled.
     */
    void GenerateGeometry();

    /**
     * @brief Collects polygons for a specific entity.
     * @param entityID The ID of the entity to process.
     * @param cb Callback invoked with the resulting faces.
     */
    void GatherPolygons(int entityID, const polygonGatherCb &cb);

    /**
     * @brief Retrieves faces for an entity filtered by texture.
     * @param entityID The ID of the entity.
     * @param texName The name of the texture to filter by.
     * @return A vector of pointers to matching faces.
     */
    std::vector<FacePtr> PolygonsByTexture(int entityID, const std::string &texName);

    /**
     * @brief Gets the list of WAD files referenced by the map.
     * @return Vector of WAD filenames.
     */
    const std::vector<std::string> &Wads() const { return m_map_file->m_wads; };

    /**
     * @brief Checks if the map references any WAD files.
     * @return True if WADs are present, false otherwise.
     */
    bool HasMapFile() const { return m_map_file != nullptr; }

    bool HasWads() const { return !m_map_file->m_wads.empty(); };

    /**
     * @brief Gets all texture names used in the map.
     * @return Vector of texture names.
     */
    const std::vector<std::string> &TextureNames() const { return m_map_file->m_textures; };

    /**
     * @brief Gets a texture name by its internal ID.
     * @param textureID The numeric ID of the texture.
     * @return The name of the texture.
     */
    const std::string &TextureName(int textureID);

    /**
     * @brief Accesses the low-level map file data structure.
     * @return Pointer to the internal QMapFile.
     */
    QMapFile *MapData() { return m_map_file.get(); };

    /**
     * @brief Overrides the face type for a specific texture.
     *
     * Useful for assigning special properties (e.g., sky, liquids) based on texture name.
     * @param texture The texture name.
     * @param type The new face type to assign.
     */
    void SetFaceTypeByTextureID(const std::string &texture, MapSurface::eFaceType type);

    /**
     * @brief Gets the WorldSpawn entity (the static world).
     * @return Pointer to the WorldSpawn entity.
     */
    const SolidMapEntity *WorldSpawn() { return m_map_file->m_worldSpawn; }

    /**
     * @brief Gets the list of solid (brush-based) entities.
     * @return Vector of pointers to solid entities.
     */
    const std::vector<SolidEntityPtr> &SolidEntities() const { return m_map_file->m_solidEntities; };

    /**
     * @brief Gets the list of point entities.
     * @return Vector of pointers to point entities.
     */
    const std::vector<PointEntityPtr> &PointEntities() const { return m_map_file->m_pointEntities; };

    /**
     * @brief Finds point entities by their classname.
     * @param className The classname to search for (case sensitive usually).
     * @return Vector of matching point entites.
     */
    std::vector<PointEntityPtr> PointEntitiesByClass(const std::string &className);

  private:
    bool getPolygonsByTextureID(int entityID, int texID, std::vector<FacePtr> &list);

    std::map<int, MapSurface::eFaceType> m_textureIDTypes;
    std::map<int, textureBounds> m_textureIDBounds;
    std::shared_ptr<QMapFile> m_map_file;
    QMapConfig m_config;
  };
} // namespace quakelib::map
