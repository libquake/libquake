#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <quakelib/wad/wad_manager.h>

namespace quakelib::wad {

  QuakeWadManager::QuakeWadManager() {}

  QuakeWadManager::~QuakeWadManager() {}

  namespace {
    std::string lower(std::string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    }

    std::string canonical(const std::string &path) {
      std::error_code ec;
      auto p = std::filesystem::weakly_canonical(path, ec);
      return ec ? path : p.string();
    }
  } // namespace

  bool QuakeWadManager::AddWadFile(const std::string &path) {
    auto key = canonical(path);
    if (std::find(files.begin(), files.end(), key) != files.end())
      return false;
    try {
      auto wad = QuakeWad::FromFile(path);
      if (wad) {
        wads.push_back(wad);
        files.push_back(key);
        return true;
      }
      std::cerr << "Not a WAD2 file: " << path << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Failed to load WAD " << path << ": " << e.what() << std::endl;
    }
    return false;
  }

  size_t QuakeWadManager::AddWadDirectory(const std::string &dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> found;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
      if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".wad")
        found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end(), [](const fs::path &a, const fs::path &b) {
      return lower(a.filename().string()) < lower(b.filename().string());
    });
    size_t added = 0;
    for (const auto &p : found)
      added += AddWadFile(p.string()) ? 1 : 0;
    return added;
  }

  std::string QuakeWadManager::FindWadFile(const std::string &name, const std::vector<std::string> &dirs) {
    namespace fs = std::filesystem;
    // the wad key may carry a path from the mapper's machine, only the file name counts
    auto slash = name.find_last_of("/\\");
    const std::string wanted = lower(slash == std::string::npos ? name : name.substr(slash + 1));
    if (wanted.empty())
      return {};
    for (const auto &dir : dirs) {
      std::error_code ec;
      if (dir.empty() || !fs::is_directory(dir, ec))
        continue;
      for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && lower(entry.path().filename().string()) == wanted)
          return entry.path().string();
      }
    }
    return {};
  }

  QuakeTexture *QuakeWadManager::FindTexture(const std::string &name) {
    for (auto &wad : wads) {
      auto tex = wad->GetTexture(name);
      if (tex) {
        return tex;
      }
    }
    return nullptr;
  }

} // namespace quakelib::wad
