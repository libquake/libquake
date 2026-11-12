#pragma once
#include "map.h"
#include <array>
#include <map>
#include <optional>
#include <quakelib/lightmap_baker.h>
#include <quakelib/lightmap_layout.h>
#include <quakelib/map_provider.h>

namespace quakelib {

  /**
   * @brief Map provider implementation for MAP source files.
   *
   * QMapProvider implements the IMapProvider interface to load Quake
   * source map files (.map). It handles parsing, CSG operations, and
   * geometry generation for editable map files.
   *
   * This allows applications to work with both source MAP files and
   * compiled BSP files using the same interface.
   *
   * @see IMapProvider
   * @see quakelib::map::QMap
   */
  class QMapProvider : public IMapProvider {
  public:
    using LightmapUVOptions = quakelib::LightmapUVOptions;

    /**
     * @brief Default constructor.
     */
    QMapProvider() = default;

    /**
     * @brief Virtual destructor.
     */
    virtual ~QMapProvider() = default;

    /**
     * @brief Load a MAP file with default configuration.
     * @param path Path to the .map file.
     * @return True on success, false on failure.
     */
    bool Load(const std::string &path) override;

    /**
     * @brief Load a MAP file with custom configuration.
     * @param path Path to the .map file.
     * @param cfg Configuration options for loading and processing.
     * @return True on success, false on failure.
     */
    bool Load(const std::string &path, const quakelib::map::QMapConfig &cfg);
    void GenerateGeometry(bool csg = true) override;
    void SetFaceType(const std::string &textureName, SurfaceType type) override;

    std::vector<SolidEntityPtr> GetSolidEntities() const override;
    std::vector<SolidEntityPtr> GetSolidEntities(const std::string &className) const override;
    std::vector<PointEntityPtr> GetPointEntities() const override;
    std::vector<PointEntityPtr> GetPointEntities(const std::string &className) const override;
    std::vector<std::string> GetTextureNames() const override;

    std::vector<RenderMesh> GetEntityMeshes(const SolidEntityPtr &entity) override;

    /**
     * @brief One hull per brush from the unclipped brushes. Hint and origin brushes are left out.
     */
    std::vector<CollisionHull> GetEntityCollisionHulls(const SolidEntityPtr &entity) override;

    std::vector<std::string> GetRequiredWads() const override;
    void SetTextureBoundsProvider(std::function<std::pair<int, int>(const std::string &)> provider) override;

    void SetLightmapUVOptions(const LightmapUVOptions &opts);

    /**
     * @brief Configuration passed to Load().
     */
    const map::QMapConfig &Config() const { return m_map.Config(); }

    /**
     * @brief Size of the lightmap atlas. Planar: the shared atlas, XAtlas: the last GetEntityMeshes() call.
     */
    int LightmapWidth() const { return m_lightmapWidth; }

    int LightmapHeight() const { return m_lightmapHeight; }

    /**
     * @brief Chart layout shared by all entities (Planar method), built on first use.
     */
    const LightmapLayout &GetLightmapLayout();

    /**
     * @brief Lights of the map's light entities plus the extra lights, and the worldspawn bake settings.
     */
    std::vector<BakeLight> GetMapLights(BakeOptions *optionsOut = nullptr);

    /**
     * @brief Lights baked in addition to the map's own, e.g. engine lights or lights of a neighbouring map
     * segment.
     */
    void SetExtraLights(std::vector<BakeLight> lights);

    /**
     * @brief Geometry that casts shadows in addition to this map, e.g. neighbouring map segments.
     */
    void SetExtraOccluders(BakeOccluders occluders);

    /**
     * @brief Replaces the settings read from the worldspawn (minlight, anglescale, ...).
     */
    void SetBakeOptions(std::optional<BakeOptions> options);

    /**
     * @brief Bakes the lightmap on first use (Planar method only), later calls return the cached result.
     */
    const BakedLightmap &GetBakedLightmap();

    /**
     * @brief The baked lightmap with the current light style values, RGBA in the atlas of
     * GetLightmapLayout().
     */
    std::optional<TextureData> GetLightmapData() const override;
    void SetLightStyle(int style, const std::string &pattern) override;
    bool UpdateLightStyles(double time) override;

  private:
    BakeOccluders buildOccluders();
    void ensureLightmapLayout();
    void invalidateMeshes();


    quakelib::map::QMap m_map;
    std::vector<LevelEntity> m_entities;
    LightmapUVOptions m_lightmapOptions;
    LightmapLayout m_layout;
    bool m_layoutValid = false;
    std::map<const SolidEntity *, std::vector<RenderMesh>> m_meshCache;
    std::vector<BakeLight> m_extraLights;
    BakeOccluders m_extraOccluders;
    std::optional<BakeOptions> m_bakeOptions;
    BakedLightmap m_baked;
    bool m_bakedValid = false;
    LightStyleAnimator m_styles;
    std::vector<uint8_t> m_composed;
    int m_lightmapWidth = 0;
    int m_lightmapHeight = 0;
  };
} // namespace quakelib
