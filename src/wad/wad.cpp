#include <quakelib/wad/wad.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <locale>
#include <vector>

namespace quakelib::wad {
  static const int MAGIC_LEN = 4;

  std::string to_lower(std::string s) {
    for (char &c : s)
      c = tolower(c);
    return s;
  }

  bool QuakeWad::IsSkyTexture(const std::string texname) {
    // the engine only checks the prefix
    return to_lower(texname).rfind("sky", 0) == 0;
  }

  QuakeWadPtr QuakeWad::FromFile(const std::string &fileName, QuakeWadOptions opts) {
    auto w = std::make_shared<QuakeWad>();
    w->opts = opts;
    w->m_istream.open(fileName, std::ios::binary);
    char magic[MAGIC_LEN + 1] = {};
    w->m_istream.read(magic, MAGIC_LEN);
    if (magic[0] == '\0') {
      return nullptr;
    }
    if (std::string(magic) != "WAD2")
      throw std::runtime_error("WAD magic string malformed");

    w->m_istream.read(reinterpret_cast<char *>(&w->m_numEntries), sizeof(uint32_t));
    w->m_istream.read(reinterpret_cast<char *>(&w->m_dirOffset), sizeof(uint32_t));
    w->m_istream.seekg(w->m_dirOffset, w->m_istream.beg);

    for (unsigned int i = 0; i < w->m_numEntries; i++) {
      auto we = QuakeWadEntry();
      w->m_istream.read(reinterpret_cast<char *>(&we.header), sizeof(QuakeWadEntry::header));
      char name[TEXTURE_NAME_LENGTH + 1] = {};
      w->m_istream.read(name, TEXTURE_NAME_LENGTH);
      we.name = std::string(name);
      w->m_entries[we.name] = we;
    }
    return w;
  }

  QuakeWad::~QuakeWad() {
    if (m_istream.is_open())
      m_istream.close();
  }

  QuakeTexture *QuakeWad::GetTexture(const std::string &textureName) {
    if (m_entries.find(textureName) == m_entries.end())
      return nullptr;

    auto &qwe = m_entries[textureName];

    if (qwe.texture.raw.size() == 0) {
      m_istream.seekg(qwe.header.offset + TEXTURE_NAME_LENGTH, m_istream.beg);
      m_istream.read((char *)&qwe.texture.width, sizeof(uint32_t));
      m_istream.read((char *)&qwe.texture.height, sizeof(uint32_t));
      m_istream.read((char *)&qwe.texture.mipOffsets, sizeof(uint32_t) * MAX_MIP_LEVELS);
      std::vector<uint8_t> buff(qwe.texture.width * qwe.texture.height);
      m_istream.read((char *)buff.data(), buff.size());

      // FromBuffer allocates with new, so we need to copy and delete
      QuakeTexture *temp =
          FromBuffer(buff.data(), IsSkyTexture(textureName), qwe.texture.width, qwe.texture.height);
      qwe.texture = *temp;
      delete temp;
    }

    return &qwe.texture;
  }

  QuakeTexture *QuakeWad::FromBuffer(const uint8_t *buff, bool isSky, int width, int height) {
    QuakeTexture *qtex;
    if (isSky) {
      qtex = new QuakeSkyTexture();
    } else {
      qtex = new QuakeTexture();
    }
    qtex->width = width;
    qtex->height = height;
    qtex->FillTextureData(buff, width * height, opts.flipTexHorizontal, m_pal);
    return qtex;
  }
} // namespace quakelib::wad