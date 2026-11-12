#pragma once

#include "entities.h"
#include "level.h"
#include "vertex.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quakelib {

  struct TextureData {
    int width;
    int height;
    std::vector<unsigned char> data; // RGBA
    /// black except the fullbright pixels, empty if there are none
    std::vector<unsigned char> emission;
  };

  class IMapProvider {
  public:
    virtual ~IMapProvider() = default;

    virtual bool Load(const std::string &path) = 0;
    virtual void GenerateGeometry(bool csg = true) = 0;
    virtual void SetFaceType(const std::string &textureName, SurfaceType type) = 0;

    // Entity access
    virtual std::vector<SolidEntityPtr> GetSolidEntities() const = 0;
    virtual std::vector<SolidEntityPtr> GetSolidEntities(const std::string &className) const = 0;
    virtual std::vector<PointEntityPtr> GetPointEntities() const = 0;
    virtual std::vector<PointEntityPtr> GetPointEntities(const std::string &className) const = 0;

    virtual std::vector<std::string> GetTextureNames() const = 0;

    virtual std::vector<RenderMesh> GetEntityMeshes(const SolidEntityPtr &entity) = 0;

    /**
     * @brief Convex hulls of the entity's brushes for physics. Not every format has brushes (BSP), those
     * return none.
     */
    virtual std::vector<CollisionHull> GetEntityCollisionHulls(const SolidEntityPtr &entity) { return {}; }

    virtual std::vector<std::string> GetRequiredWads() const { return {}; }

    virtual void SetTextureBoundsProvider(std::function<std::pair<int, int>(const std::string &)> provider) {}

    virtual std::optional<TextureData> GetTextureData(const std::string &name) const { return std::nullopt; }

    virtual std::optional<TextureData> GetLightmapData() const { return std::nullopt; }

    /**
     * @brief Change a light style pattern ('a'..'z'), e.g. "a" to switch style 32 off.
     */
    virtual void SetLightStyle(int style, const std::string &pattern) {}

    /**
     * @brief Advance animated light styles to the given time in seconds.
     * @return true if the lightmap changed and GetLightmapData() should be uploaded again.
     */
    virtual bool UpdateLightStyles(double time) { return false; }
  };

  using IMapProviderPtr = std::shared_ptr<IMapProvider>;

} // namespace quakelib
