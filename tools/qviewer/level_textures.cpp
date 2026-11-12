#include "level_textures.h"

#include <cstring>
#include <filesystem>
#include <iostream>

namespace {
  Image imageFromRGBA(const uint8_t *rgba, int width, int height) {
    size_t size = (size_t)width * height * 4;
    Image img{MemAlloc((unsigned int)size), width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    std::memcpy(img.data, rgba, size);
    return img;
  }

  Image imageFromColors(const quakelib::wad::cvec &colors, int width, int height) {
    static_assert(sizeof(quakelib::wad::color) == 4);
    return imageFromRGBA(colors.front().rgba, width, height);
  }

  Texture2D singleTexel(Color color) {
    Image img = GenImageColor(1, 1, color);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
  }
} // namespace

void LevelTextures::LoadWads(const std::vector<std::string> &named, const std::vector<std::string> &wadPaths,
                             const std::string &mapDir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<std::string> dirs;
  for (const auto &p : wadPaths)
    if (fs::is_directory(p, ec))
      dirs.push_back(p);
  dirs.push_back(mapDir.empty() ? "." : mapDir);

  // the wads named by the map come first, so their textures win
  for (const auto &name : named) {
    auto path = quakelib::wad::QuakeWadManager::FindWadFile(name, dirs);
    if (path.empty())
      std::cout << "WAD " << name << " named by the map not found, using the others" << std::endl;
    else
      wads.AddWadFile(path);
  }
  for (const auto &p : wadPaths) {
    if (fs::is_directory(p, ec))
      wads.AddWadDirectory(p);
    else if (!wads.AddWadFile(p) && !fs::exists(p, ec))
      std::cout << "WAD path not found: " << p << std::endl;
  }
  for (const auto &f : wads.LoadedFiles())
    std::cout << "WAD " << f << std::endl;
}

std::pair<int, int> LevelTextures::Size(const std::string &name) {
  if (auto *qt = wads.FindTexture(name))
    return {(int)qt->width, (int)qt->height};
  return {0, 0};
}

bool LevelTextures::fromLevel(const quakelib::TextureInfo &info, Images &out) {
  if (info.rgba.empty())
    return false;
  // sky textures arrive split, rgba is the back layer
  out.albedo = imageFromRGBA(info.rgba.data(), info.width, info.height);
  if (!info.skyFront.empty())
    out.skyFront = imageFromRGBA(info.skyFront.data(), info.width, info.height);
  if (info.hasEmission)
    out.emission = imageFromRGBA(info.emission.data(), info.width, info.height);
  return true;
}

bool LevelTextures::fromWad(const std::string &name, Images &out) {
  auto *qt = wads.FindTexture(name);
  if (!qt || qt->raw.empty())
    return false;
  // wad skies are already split, raw is the back layer
  out.albedo = imageFromColors(qt->raw, (int)qt->width, (int)qt->height);
  if (!qt->front.empty())
    out.skyFront = imageFromColors(qt->front, (int)qt->width, (int)qt->height);
  if (qt->hasEmission)
    out.emission = imageFromColors(qt->emission, (int)qt->width, (int)qt->height);
  return true;
}

Texture2D LevelTextures::upload(Image &img, bool mipmaps) {
  if (mipmaps)
    ImageMipmaps(&img);
  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  img = Image{};
  SetTextureFilter(tex, mipmaps ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
  textures.push_back(tex);
  return tex;
}

Shader LevelTextures::shaderFor(Kind kind, const SurfaceShaders &shaders) {
  switch (kind) {
  case Kind::Alpha:
    return shaders.alpha;
  case Kind::Sky:
    return shaders.sky;
  default:
    return shaders.world;
  }
}

void LevelTextures::Build(const quakelib::Level &level, const SurfaceShaders &shaders, Texture2D lightmap) {
  Unload();
  black = singleTexel(BLACK);
  transparent = singleTexel(BLANK);

  for (const auto &info : level.textures) {
    Images img;
    if (!fromLevel(info, img) && !fromWad(info.name, img)) {
      // a checker keeps the texture mapping visible, tool textures are not drawn anyway
      if (!info.name.empty() && quakelib::DefaultSurfaceType(info.name) == quakelib::SurfaceType::SOLID)
        missing.push_back(info.name);
      img.albedo = GenImageChecked(64, 64, 16, 16, Color{90, 90, 90, 255}, Color{150, 150, 150, 255});
    }

    Material mat = LoadMaterialDefault();
    Kind kind = quakelib::IsSkyTexture(info.name) ? Kind::Sky : info.hasAlpha ? Kind::Alpha : Kind::World;
    mat.shader = shaderFor(kind, shaders);
    if (kind == Kind::Sky) {
      mat.maps[0].texture = upload(img.albedo, false);
      mat.maps[1].texture = img.skyFront.data ? upload(img.skyFront, false) : transparent;
    } else {
      mat.maps[0].texture = upload(img.albedo, true);
      mat.maps[1].texture = lightmap;
      mat.maps[2].texture = img.emission.data ? upload(img.emission, true) : black;
    }
    UnloadImage(img.emission); // no-ops when already uploaded or empty
    UnloadImage(img.skyFront);
    materials.push_back(mat);
    kinds.push_back(kind);
  }
}

void LevelTextures::SetShaders(const SurfaceShaders &shaders) {
  for (size_t i = 0; i < materials.size(); i++)
    materials[i].shader = shaderFor(kinds[i], shaders);
}

void LevelTextures::SetPointFiltering(bool point) {
  for (auto &t : textures) {
    auto smooth = t.mipmaps > 1 ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR;
    SetTextureFilter(t, point ? TEXTURE_FILTER_POINT : smooth);
  }
}

void LevelTextures::Unload() {
  for (auto &t : textures)
    UnloadTexture(t);
  // the maps arrays are ours, shaders and the lightmap belong to others
  for (auto &m : materials)
    MemFree(m.maps);
  if (black.id > 0)
    UnloadTexture(black);
  if (transparent.id > 0)
    UnloadTexture(transparent);
  textures.clear();
  materials.clear();
  kinds.clear();
  missing.clear();
  black = transparent = Texture2D{};
}
