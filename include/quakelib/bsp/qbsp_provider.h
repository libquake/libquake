#pragma once
#include "qbsp.h"
#include <memory>
#include <quakelib/map_provider.h>

namespace quakelib {

  /**
   * @brief Map provider implementation for BSP files.
   *
   * QBspProvider implements the IMapProvider interface to load compiled
   * Quake BSP files. It provides access to geometry, entities, textures,
   * and lightmaps through a common interface shared with QMapProvider.
   *
   * This allows applications to work with both source MAP files and
   * compiled BSP files using the same code.
   *
   * @see IMapProvider
   * @see quakelib::bsp::QBsp
   */
  class QBspProvider : public IMapProvider {
  public:
    /**
     * @brief Default constructor.
     */
    QBspProvider();

    /**
     * @brief Virtual destructor.
     */
    virtual ~QBspProvider() = default;

    /**
     * @brief Load a BSP file with default configuration.
     * @param path Path to the .bsp file.
     * @return True on success, false on failure.
     */
    bool Load(const std::string &path) override;

    /**
     * @brief Load a BSP file with custom configuration.
     * @param path Path to the .bsp file.
     * @param cfg Configuration options for loading.
     * @return True on success, false on failure.
     */
    bool Load(const std::string &path, const quakelib::bsp::QBspConfig &cfg);
    void GenerateGeometry(bool csg = true) override;
    void SetFaceType(const std::string &textureName, SurfaceType type) override;

    std::vector<SolidEntityPtr> GetSolidEntities() const override;
    std::vector<SolidEntityPtr> GetSolidEntities(const std::string &className) const override;
    std::vector<PointEntityPtr> GetPointEntities() const override;
    std::vector<PointEntityPtr> GetPointEntities(const std::string &className) const override;
    std::vector<std::string> GetTextureNames() const override;
    std::vector<RenderMesh> GetEntityMeshes(const SolidEntityPtr &entity) override;
    std::optional<TextureData> GetTextureData(const std::string &name) const override;
    std::optional<TextureData> GetLightmapData() const override;
    void SetLightStyle(int style, const std::string &pattern) override;
    bool UpdateLightStyles(double time) override;

  private:
    std::unique_ptr<quakelib::bsp::QBsp> m_bsp;
    std::map<std::string, SurfaceType> m_faceTypes;
    LightStyleAnimator m_styles;
    std::vector<uint8_t> m_composed;
  };
} // namespace quakelib
