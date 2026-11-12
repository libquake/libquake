#pragma once

#include "bsp_file.h"
#include "entity_solid.h"
#include "lightmap.h"
#include "primitives.h"
#include <quakelib/config.h>
#include <quakelib/entities.h>

#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>

namespace quakelib::bsp {
  using EntityPtr = std::shared_ptr<quakelib::Entity>;

  enum EQBspStatus {
    QBSP_OK = 0,
    QBSP_ERR_WRONG_VERSION = -1001,
    QBSP_ERR_CANNOT_OPEN = -1002,
  };

  struct bspTexure {
    bspTexure() = default;

    bspTexure(const miptex_t &mt) {
      width = mt.width;
      height = mt.height;
      name = string(mt.name, strnlen(mt.name, MAX_TEXNAME));
    };

    std::string name;
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool hasData = false;
    unsigned char *data = nullptr;
  };

  /**
   * @brief Configuration options for loading BSP files.
   *
   * Extends the base Config with BSP-specific options for controlling
   * which data is loaded and how it's processed.
   */
  struct QBspConfig : public Config {
    /**
     * @brief Load the texture lump from the BSP file.
     *
     * When enabled, reads texture definitions including names, dimensions,
     * and mipmap offsets from the BSP file.
     */
    bool loadTextures = true;

    /**
     * @brief Extract pixel data from textures.
     *
     * When enabled along with loadTextures, extracts the actual RGBA pixel
     * data for each texture. Set to false if you only need texture metadata
     * (names and dimensions) to reduce memory usage.
     */
    bool loadTextureData = true;
  };

  /**
   * @brief Quake BSP file loader and processor.
   *
   * QBsp handles loading compiled Quake BSP (Binary Space Partition) files.
   * BSP files contain pre-calculated geometric data, textures, lighting, and
   * entity definitions needed to efficiently render Quake levels.
   *
   * BSP files include:
   * - Pre-calculated BSP tree for spatial partitioning
   * - Embedded mip-mapped textures
   * - Pre-computed lightmaps
   * - Visibility data (PVS)
   * - Entity definitions
   * - Collision geometry
   *
   * @see QBspConfig for loading options
   * @see https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_4.htm
   */
  class QBsp {
  public:
    /**
     * @brief Default constructor.
     */
    QBsp() = default;

    /**
     * @brief Construct with configuration.
     * @param cfg Configuration options for loading and processing.
     */
    QBsp(QBspConfig cfg) : m_config(cfg) {};

    /**
     * @brief Destructor.
     */
    ~QBsp() = default;

    /**
     * @brief Load a BSP file from disk.
     *
     * Loads all lumps from the BSP file including geometry, textures,
     * entities, and lighting data according to the configuration.
     *
     * @param filename Path to the .bsp file.
     * @return QBSP_OK on success, or an error code (e.g., QBSP_ERR_WRONG_VERSION).
     */
    int LoadFile(const char *filename);

    /**
     * @brief Get the BSP file format version.
     * @return Version number (typically 29 or 30 for Quake).
     */
    uint32_t Version() const;

    /**
     * @brief Get the worldspawn entity.
     *
     * The worldspawn represents the main level geometry and global properties.
     *
     * @return Pointer to the worldspawn entity.
     */
    const SolidEntityPtr WorldSpawn() const;

    /**
     * @brief Get all entities organized by classname.
     * @return Map of classname to list of entities with that classname.
     */
    const std::map<string, vector<EntityPtr>> &Entities() const;

    /**
     * @brief Iterate entities of a specific classname.
     * @param className The classname to filter by.
     * @param cb Callback function called for each matching entity.
     * @return True if iteration completed, false if callback returned false.
     */
    bool Entities(const string &className, std::function<bool(EntityPtr)> cb) const;

    /**
     * @brief Get all point entities.
     *
     * Point entities have no geometry (e.g., lights, spawn points, monsters).
     *
     * @return Vector of point entity pointers.
     */
    const vector<EntityPtr> &PointEntities() const;

    /**
     * @brief Get all solid (brush-based) entities.
     *
     * Solid entities have geometry (e.g., doors, platforms, triggers).
     *
     * @return Vector of solid entity pointers.
     */
    const vector<SolidEntityPtr> &SolidEntities() const;

    /**
     * @brief Safely cast an entity to a solid entity.
     * @param ent Entity to cast.
     * @return Solid entity pointer, or nullptr if not a solid entity.
     */
    static const SolidEntityPtr ToSolidEntity(EntityPtr ent);

    /**
     * @brief Get raw BSP file content.
     *
     * Provides direct access to the loaded BSP lumps including vertices,
     * edges, faces, planes, nodes, leaves, etc.
     *
     * @return Reference to the BSP file content structure.
     */
    const bspFileContent &Content() const;

    /**
     * @brief Get all textures from the BSP file.
     *
     * Returns texture metadata including names, dimensions, and pixel data
     * (if loadTextureData was enabled in config).
     *
     * @return Vector of texture structures.
     */
    const vector<bspTexure> &Textures() const;

    /**
     * @brief Get the lightmap data.
     *
     * Returns the packed lightmap atlas containing lighting data for all faces.
     *
     * @return Pointer to lightmap object, or nullptr if no lightmap data.
     */
    const Lightmap *LightMap() const;

    /// raw text of the entity lump
    const std::string &EntityLump() const { return m_entityLump; }

    /**
     * @brief Mutable lightmap access, used to change and animate light styles.
     */
    Lightmap *LightMap();

  private:
    void parseEntities(const char *entsrc);
    int loadTextureInfo();
    void prepareLevel();
    void prepareLightMaps();
    void loadTexelBuff(unsigned char **buffOut, uint32_t offset, uint32_t len);

    std::ifstream m_istream;
    QBspConfig m_config;
    string m_mapPath = "";

    vector<EntityPtr> m_pointEntities;
    std::map<string, vector<EntityPtr>> m_entities;
    vector<SolidEntityPtr> m_solidEntities;

    vector<bspTexure> m_textures;
    SolidEntityPtr m_worldSpawn;

    bspFileContent m_content;
    std::string m_entityLump;
    std::unique_ptr<Lightmap> m_lm;
  };
} // namespace quakelib::bsp