#pragma once

#include <quakelib/level.h>
#include <quakelib/wad/wad_manager.h>
#include <raylib.h>
#include <string>
#include <vector>

/**
 * @brief Textures and materials of a level, one material per entry of Level::textures.
 *
 * Material maps as the shaders see them:
 *   walls: texture0 albedo, texture1 lightmap, texture2 emission (fullbright pixels)
 *   sky:   texture0 back layer, texture1 front layer
 */
class LevelTextures {
public:
  /// the wads named by the map first (found case insensitive in the wad folders and next to the map), then
  /// every wad of wadPaths as a pool for the rest
  void LoadWads(const std::vector<std::string> &named, const std::vector<std::string> &wadPaths,
                const std::string &mapDir);

  /// size of a wad texture, {0, 0} if unknown
  std::pair<int, int> Size(const std::string &name);

  void Build(const quakelib::Level &level, Shader worldShader, Shader skyShader, Texture2D lightmap);
  void SetShaders(Shader worldShader, Shader skyShader);
  void SetPointFiltering(bool point);
  void Unload();

  Material *Materials() { return materials.data(); }

  int Count() const { return (int)materials.size(); }

  /// textures of the last Build that were found nowhere
  const std::vector<std::string> &Missing() const { return missing; }

private:
  struct Images {
    Image albedo{};
    Image emission{};
    Image skyFront{};
  };

  bool fromLevel(const quakelib::TextureInfo &info, Images &out);
  bool fromWad(const std::string &name, Images &out);
  Texture2D upload(Image &img, bool mipmaps);

  quakelib::wad::QuakeWadManager wads;
  std::vector<Material> materials;
  std::vector<bool> isSky;
  std::vector<Texture2D> textures;
  std::vector<std::string> missing;
  Texture2D black{};
  Texture2D transparent{};
};
