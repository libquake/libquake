#include <cstring>
#include <filesystem>
#include <fstream>
#include <quakelib/wad/wad.h>
#include <quakelib/wad/wad_manager.h>
#include <snitch/snitch.hpp>

using namespace quakelib::wad;

namespace {
  // writes a WAD2 with 4x4 mip textures, every pixel set to the given palette index
  std::string writeTestWad(const std::string &file,
                           const std::vector<std::pair<std::string, uint8_t>> &textures) {
    auto path = (std::filesystem::temp_directory_path() / file).string();
    std::vector<char> lumps;
    std::vector<uint32_t> offsets;
    for (const auto &[name, index] : textures) {
      offsets.push_back(12 + (uint32_t)lumps.size());
      char header[40] = {};
      std::strncpy(header, name.c_str(), 16);
      uint32_t fields[6] = {4, 4, 40, 40 + 16, 40 + 20, 40 + 21};
      std::memcpy(header + 16, fields, sizeof(fields));
      lumps.insert(lumps.end(), header, header + 40);
      lumps.insert(lumps.end(), 16 + 4 + 1 + 1, (char)index);
    }
    std::ofstream out(path, std::ios::binary);
    uint32_t count = (uint32_t)textures.size(), dir = 12 + (uint32_t)lumps.size();
    out.write("WAD2", 4);
    out.write((const char *)&count, 4);
    out.write((const char *)&dir, 4);
    out.write(lumps.data(), (std::streamsize)lumps.size());
    for (size_t i = 0; i < textures.size(); i++) {
      char entry[32] = {};
      uint32_t size = 40 + 22;
      std::memcpy(entry, &offsets[i], 4);
      std::memcpy(entry + 4, &size, 4);
      std::memcpy(entry + 8, &size, 4);
      entry[12] = QuakeWadEntry::QWET_MipsTexture;
      std::strncpy(entry + 16, textures[i].first.c_str(), 16);
      out.write(entry, 32);
    }
    return path;
  }
} // namespace

TEST_CASE("WAD - Load file", "[wad]") {
  const char *wadPath = "tests/data/prototype.wad";

  SECTION("Load WAD file") {
    QuakeWadOptions opts;
    opts.flipTexHorizontal = false;

    auto wad = QuakeWad::FromFile(wadPath, opts);
    REQUIRE(wad != nullptr);

    const auto &textures = wad->Textures();
    CHECK(textures.size() > 0);

    CAPTURE(textures.size());

    // Note: Textures are loaded lazily - width/height are 0 until GetTexture() is called
    // So we need to actually get textures to validate them

    size_t validTexCount = 0;
    size_t skyTexCount = 0;
    size_t testedCount = 0;

    for (const auto &[name, entry] : textures) {
      // Test a sample of textures (not all 273)
      if (testedCount >= 10)
        break;
      testedCount++;

      QuakeTexture *tex = wad->GetTexture(name);
      if (tex != nullptr) {
        CAPTURE(name);
        CAPTURE(tex->width);
        CAPTURE(tex->height);
        CAPTURE(tex->type);
        CAPTURE(tex->raw.size());

        if (tex->width > 0 && tex->height > 0) {
          validTexCount++;

          // Check that raw data size matches dimensions
          // raw is std::vector<color> where each color is 4 bytes (RGBA)
          size_t expectedPixels = tex->width * tex->height;
          CHECK(tex->raw.size() == expectedPixels);

          if (tex->type == TTYPE_SKY_TEXTURE) {
            skyTexCount++;
          }
        }
      }
    }

    CAPTURE(validTexCount);
    CAPTURE(skyTexCount);
    CAPTURE(testedCount);

    // Should have at least some valid textures
    CHECK(validTexCount > 0);
  }

  SECTION("Get specific texture by name") {
    auto wad = QuakeWad::FromFile(wadPath);
    REQUIRE(wad != nullptr);

    // Try to get a texture that should exist based on the map file
    QuakeTexture *tex = wad->GetTexture("128_blue_3");

    if (tex != nullptr) {
      CHECK(tex->width > 0);
      CHECK(tex->height > 0);

      size_t expectedPixels = tex->width * tex->height;
      CHECK(tex->raw.size() == expectedPixels);

      CAPTURE(tex->width);
      CAPTURE(tex->height);
      CAPTURE(tex->type);
      CAPTURE(tex->raw.size());
      CAPTURE(expectedPixels);

      // Verify raw data size in bytes
      size_t rawByteSize = tex->raw.size() * sizeof(color);
      CAPTURE(rawByteSize);
      CHECK(rawByteSize == expectedPixels * 4);
    } else {
      // If not found, list what textures ARE available
      const auto &textures = wad->Textures();
      for (const auto &[name, entry] : textures) {
        CAPTURE(name);
      }
      FAIL("Texture '128_blue_3' not found in WAD");
    }
  }

  SECTION("Get non-existent texture") {
    auto wad = QuakeWad::FromFile(wadPath);
    REQUIRE(wad != nullptr);

    QuakeTexture *tex = wad->GetTexture("DOESNOTEXIST");
    CHECK(tex == nullptr);
  }

  SECTION("Load with horizontal flip") {
    QuakeWadOptions opts;
    opts.flipTexHorizontal = true;

    auto wad = QuakeWad::FromFile(wadPath, opts);
    REQUIRE(wad != nullptr);

    CHECK(wad->Textures().size() > 0);
  }
}

TEST_CASE("WAD - Sky texture detection", "[wad]") {
  CHECK(QuakeWad::IsSkyTexture("sky1") == true);
  CHECK(QuakeWad::IsSkyTexture("sky2") == true);
  CHECK(QuakeWad::IsSkyTexture("skybox") == true);
  CHECK(QuakeWad::IsSkyTexture("METAL1") == false);
  CHECK(QuakeWad::IsSkyTexture("128_blue_3") == false);
}

TEST_CASE("WAD - case insensitive lookup", "[wad]") {
  auto wad = QuakeWad::FromFile("tests/data/prototype.wad", QuakeWadOptions());
  REQUIRE(wad != nullptr);
  auto *exact = wad->GetTexture("text_A");
  REQUIRE(exact != nullptr);
  CHECK(wad->GetTexture("TEXT_a") == exact);
  CHECK(wad->GetTexture("text_a") == exact);
  CHECK(wad->GetTexture("SKY1") == wad->GetTexture("sky1"));
  CHECK(wad->GetTexture("text_") == nullptr);
}

TEST_CASE("WAD - alpha only on '{' textures", "[wad]") {
  REQUIRE(quakelib::ClassifyTexture("{fence") == quakelib::TEXTURE_ALPHA);
  REQUIRE(quakelib::ClassifyTexture("*water1") == quakelib::TEXTURE_LIQUID);
  REQUIRE(quakelib::ClassifyTexture("common/SKY4") == quakelib::TEXTURE_SKY);
  REQUIRE(quakelib::ClassifyTexture("metal{1") == 0);
  REQUIRE(quakelib::ClassifyTexture("") == 0);

  auto path = writeTestWad("quakelib_alpha_test.wad", {{"{fence", 255}, {"opaque", 255}, {"{solid", 7}});
  auto wad = QuakeWad::FromFile(path, QuakeWadOptions());
  REQUIRE(wad != nullptr);

  auto *fence = wad->GetTexture("{fence");
  REQUIRE(fence != nullptr);
  REQUIRE(fence->hasAlpha);
  REQUIRE(fence->raw.size() == 16);
  for (const auto &c : fence->raw) {
    REQUIRE(c.rgba[3] == 0);
    REQUIRE((c.rgba[0] | c.rgba[1] | c.rgba[2]) == 0);
  }

  // index 255 is an ordinary color everywhere else
  auto *opaque = wad->GetTexture("opaque");
  REQUIRE(opaque != nullptr);
  REQUIRE_FALSE(opaque->hasAlpha);
  for (const auto &c : opaque->raw)
    REQUIRE(c.rgba[3] == 255);

  auto *solid = wad->GetTexture("{solid");
  REQUIRE(solid != nullptr);
  REQUIRE(solid->hasAlpha);
  for (const auto &c : solid->raw)
    REQUIRE(c.rgba[3] == 255);

  REQUIRE(default_palette.GetColor(255).rgba[3] == 255);
  wad.reset();
  std::filesystem::remove(path);
}

TEST_CASE("WAD - Invalid file", "[wad]") {
  auto wad = QuakeWad::FromFile("nonexistent.wad");
  CHECK(wad == nullptr);
}

TEST_CASE("WAD - fullbright emission", "[wad]") {
  QuakeWadOptions opts;
  opts.flipTexHorizontal = false;
  auto wad = QuakeWad::FromFile("tests/data/prototype.wad", opts);
  REQUIRE(wad != nullptr);

  REQUIRE_FALSE(Palette::IsFullbright(223));
  REQUIRE(Palette::IsFullbright(224));
  REQUIRE(Palette::IsFullbright(254));
  REQUIRE_FALSE(Palette::IsFullbright(255));

  // the button has 884 fullbright pixels, the emission keeps their color and is black elsewhere
  auto *button = wad->GetTexture("+0button_0");
  REQUIRE(button != nullptr);
  REQUIRE(button->hasEmission);
  REQUIRE(button->emission.size() == button->raw.size());
  size_t lit = 0;
  for (size_t i = 0; i < button->emission.size(); i++) {
    const auto &e = button->emission[i].rgba;
    REQUIRE(e[3] == 255);
    if (e[0] || e[1] || e[2]) {
      lit++;
      for (int k = 0; k < 3; k++)
        REQUIRE(e[k] == button->raw[i].rgba[k]);
    }
  }
  REQUIRE(lit == 884);

  auto *water = wad->GetTexture("*64_water1_1");
  REQUIRE(water != nullptr);
  REQUIRE_FALSE(water->hasEmission);
  REQUIRE(water->emission.empty());

  // sky is drawn without lighting, no emission
  auto *sky = wad->GetTexture("sky1");
  REQUIRE(sky != nullptr);
  REQUIRE_FALSE(sky->hasEmission);
}

TEST_CASE("WAD - sky layers", "[wad]") {
  QuakeWadOptions opts;
  opts.flipTexHorizontal = false;
  auto wad = QuakeWad::FromFile("tests/data/prototype.wad", opts);
  REQUIRE(wad != nullptr);
  REQUIRE(QuakeWad::IsSkyTexture("SKY1"));
  REQUIRE_FALSE(QuakeWad::IsSkyTexture("*waterskip"));
  REQUIRE_FALSE(QuakeWad::IsSkyTexture("risky"));

  // 256x128 in the wad: the left half is the front layer, the right half the back layer
  auto *sky = wad->GetTexture("sky1");
  REQUIRE(sky != nullptr);
  REQUIRE(sky->type == TTYPE_SKY_TEXTURE);
  REQUIRE(sky->width == 128);
  REQUIRE(sky->height == 128);
  REQUIRE(sky->raw.size() == 128 * 128);
  REQUIRE(sky->front.size() == 128 * 128);
  size_t clear = 0;
  for (const auto &c : sky->front)
    clear += c.rgba[3] == 0;
  // palette index 0 in the left half of sky1
  REQUIRE(clear == 6686);
  for (const auto &c : sky->raw)
    REQUIRE(c.rgba[3] == 255);
}

TEST_CASE("WAD - manager folders and lookup", "[wad]") {
  // maps name wads with paths and any case, only the file name counts
  auto path = QuakeWadManager::FindWadFile("C:\\quake\\GFX\\PROTOTYPE.WAD", {"does/not/exist", "tests/data"});
  REQUIRE(!path.empty());
  REQUIRE(QuakeWadManager::FindWadFile("start.wad", {"tests/data"}).empty());

  QuakeWadManager mgr;
  // prototype.wad and sky_alpha.wad
  REQUIRE(mgr.AddWadDirectory("tests/data") == 2);
  REQUIRE(mgr.LoadedFiles().size() == 2);
  // the same file again, by folder or by path, is not loaded twice
  REQUIRE(mgr.AddWadDirectory("tests/data") == 0);
  REQUIRE_FALSE(mgr.AddWadFile(path));
  REQUIRE(mgr.FindTexture("+0button_0") != nullptr);
  REQUIRE(mgr.AddWadDirectory("does/not/exist") == 0);
}
