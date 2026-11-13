#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <quakelib/level.h>
#include <sstream>

namespace quakelib {
  namespace {
    std::string lower(std::string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    }

    const std::string &emptyString() {
      static const std::string empty;
      return empty;
    }

    Vec3 toOpenGL(const Vec3 &v) { return Vec3{v[0], v[2], -v[1]}; }
  } // namespace

  bool LevelEntity::Has(const std::string &key) const { return attributes.find(key) != attributes.end(); }

  const std::string &LevelEntity::Get(const std::string &key) const {
    auto it = attributes.find(key);
    return it == attributes.end() ? emptyString() : it->second;
  }

  float LevelEntity::Float(const std::string &key, float fallback) const {
    const auto &v = Get(key);
    if (v.empty())
      return fallback;
    char *end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    return end == v.c_str() ? fallback : f;
  }

  int LevelEntity::Int(const std::string &key, int fallback) const {
    const auto &v = Get(key);
    if (v.empty())
      return fallback;
    char *end = nullptr;
    // quake keys like spawnflags are sometimes written as floats
    double d = std::strtod(v.c_str(), &end);
    return end == v.c_str() ? fallback : (int)d;
  }

  Vec3 LevelEntity::Vector(const std::string &key, Vec3 fallback) const {
    const auto &v = Get(key);
    if (v.empty())
      return fallback;
    Vec3 out{0, 0, 0};
    const char *p = v.c_str();
    for (int i = 0; i < 3; i++) {
      char *end = nullptr;
      float f = std::strtof(p, &end);
      if (end == p)
        break;
      out[i] = f;
      p = end;
    }
    return out;
  }

  Vec3 LevelEntity::Angles() const {
    if (Has("angles"))
      return Vector("angles");
    return Vec3{0, Float("angle"), 0};
  }

  Vec3 LevelEntity::MoveDir() const {
    if (!Has("angles")) {
      float a = Float("angle");
      if (a == -1)
        return Vec3{0, 0, 1};
      if (a == -2)
        return Vec3{0, 0, -1};
    }
    Vec3 ang = Angles();
    double pitch = ang[0] * (HMM_PI / 180.0);
    double yaw = ang[1] * (HMM_PI / 180.0);
    return Vec3{(float)(std::cos(pitch) * std::cos(yaw)), (float)(std::cos(pitch) * std::sin(yaw)),
                (float)-std::sin(pitch)};
  }

  std::vector<uint8_t> &LightmapLayers::Layer(int style) {
    for (size_t i = 0; i < styles.size(); i++) {
      if (styles[i] == style)
        return layers[i];
    }
    styles.push_back(style);
    layers.emplace_back((size_t)width * height * 4, 0);
    return layers.back();
  }

  const char *DefaultLightStylePattern(int style) {
    // Quake's world.qc
    switch (style) {
    case 1:
      return "mmnmmommommnonmmonqnmmo";
    case 2:
      return "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba";
    case 3:
      return "mmmmmaaaaammmmmaaaaaabcdefgabcdefg";
    case 4:
      return "mamamamamama";
    case 5:
      return "jklmnopqrstuvwxyzyxwvutsrqponmlkj";
    case 6:
      return "nmonqnmomnmomomno";
    case 7:
      return "mmmaaaabcdefgmmmmaaaammmaamm";
    case 8:
      return "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa";
    case 9:
      return "aaaaaaaazzzzzzzz";
    case 10:
      return "mmamammmmammamamaaamammma";
    case 11:
      return "abcdefghijklmnopqrrqponmlkjihgfedcba";
    case 63:
      return "a";
    default:
      return "m";
    }
  }

  namespace {
    int patternValue(const std::string &pattern, int frame) {
      if (pattern.empty())
        return LightStyleAnimator::Normal;
      int c = pattern[(size_t)frame % pattern.size()] - 'a';
      return std::clamp(c, 0, 25) * 22;
    }

    // 'm' maps back to the stored sample value, brighter styles saturate at 255
    uint8_t toByte(uint32_t v) {
      return (uint8_t)std::min<uint32_t>((v + LightStyleAnimator::Normal / 2) / LightStyleAnimator::Normal,
                                         255u);
    }
  } // namespace

  LightStyleAnimator::LightStyleAnimator() {
    for (int i = 0; i < MaxStyles; i++) {
      m_patterns[i] = DefaultLightStylePattern(i);
      m_values[i] = patternValue(m_patterns[i], 0);
    }
    m_composedValues = m_values;
  }

  void LightStyleAnimator::SetStyle(int style, const std::string &pattern) {
    if (style >= 0 && style < MaxStyles)
      m_patterns[style] = pattern;
  }

  const std::string &LightStyleAnimator::Style(int style) const {
    return m_patterns[std::clamp(style, 0, MaxStyles - 1)];
  }

  int LightStyleAnimator::Value(int style) const {
    if (style < 0 || style >= MaxStyles)
      return 0;
    return m_values[style];
  }

  bool LightStyleAnimator::Update(double time, const LightmapLayers &lightmap) {
    int frame = std::max(0, (int)(time * 10.0));
    for (int i = 0; i < MaxStyles; i++)
      m_values[i] = patternValue(m_patterns[i], frame);

    for (int style : lightmap.styles) {
      if (style >= 0 && style < MaxStyles && m_values[style] != m_composedValues[style])
        return true;
    }
    return false;
  }

  void LightStyleAnimator::composeTexel(const LightmapLayers &lightmap, size_t texel, uint8_t *out) const {
    uint32_t sum[3] = {0, 0, 0};
    bool covered = false;
    for (size_t l = 0; l < lightmap.layers.size(); l++) {
      const uint8_t *px = lightmap.layers[l].data() + texel * 4;
      if (px[3] == 0)
        continue;
      covered = true;
      uint32_t scale = (uint32_t)Value(lightmap.styles[l]);
      for (int k = 0; k < 3; k++)
        sum[k] += px[k] * scale;
    }
    for (int k = 0; k < 3; k++)
      out[k] = toByte(sum[k]);
    out[3] = covered ? 255 : 0;
  }

  std::vector<uint8_t> LightStyleAnimator::Compose(const LightmapLayers &lightmap) {
    size_t count = (size_t)lightmap.width * lightmap.height;
    std::vector<uint8_t> rgba(count * 4, 0);
    for (size_t t = 0; t < count; t++)
      composeTexel(lightmap, t, rgba.data() + t * 4);
    m_composedValues = m_values;
    return rgba;
  }

  bool LightStyleAnimator::Recompose(const LightmapLayers &lightmap, std::vector<uint8_t> &rgba) {
    size_t count = (size_t)lightmap.width * lightmap.height;
    if (rgba.size() != count * 4) {
      rgba = Compose(lightmap);
      return true;
    }

    std::vector<const std::vector<uint8_t> *> changed;
    for (size_t l = 0; l < lightmap.layers.size(); l++) {
      int style = lightmap.styles[l];
      if (style >= 0 && style < MaxStyles && m_values[style] != m_composedValues[style])
        changed.push_back(&lightmap.layers[l]);
    }
    m_composedValues = m_values;
    if (changed.empty())
      return false;

    for (size_t t = 0; t < count; t++) {
      bool touched = false;
      for (const auto *layer : changed)
        touched |= (*layer)[t * 4 + 3] != 0;
      if (touched)
        composeTexel(lightmap, t, rgba.data() + t * 4);
    }
    return true;
  }

  const LevelEntity *Level::Worldspawn() const {
    for (const auto &e : entities) {
      if (e.classname == "worldspawn")
        return &e;
    }
    return nullptr;
  }

  int Level::FindTexture(const std::string &name) const {
    auto key = lower(name);
    for (size_t i = 0; i < textures.size(); i++) {
      if (lower(textures[i].name) == key)
        return (int)i;
    }
    return -1;
  }

  SurfaceType DefaultSurfaceType(const std::string &textureName) {
    auto name = lower(textureName);
    if (name == "clip" || name == "trigger")
      return SurfaceType::CLIP;
    for (const char *skip : {"skip", "hint", "hintskip", "nodraw", "caulk", "origin"}) {
      if (name == skip)
        return SurfaceType::SKIP;
    }
    return SurfaceType::SOLID;
  }

  void ConvertToOpenGL(Level &level) {
    for (auto &g : level.geometry) {
      for (auto &m : g.meshes) {
        for (auto &v : m.vertices) {
          v.point = toOpenGL(v.point);
          v.normal = toOpenGL(v.normal);
          Vec3 t = toOpenGL(Vec3{v.tangent[0], v.tangent[1], v.tangent[2]});
          v.tangent = Vec4{t[0], t[1], t[2], v.tangent[3]};
        }
      }
      for (auto &h : g.hulls) {
        for (auto &p : h.points)
          p = toOpenGL(p);
      }
      g.pivot = toOpenGL(g.pivot);
      Vec3 mn = g.boundsMin, mx = g.boundsMax;
      g.boundsMin = Vec3{mn[0], mn[2], -mx[1]};
      g.boundsMax = Vec3{mx[0], mx[2], -mn[1]};
    }
  }

} // namespace quakelib
