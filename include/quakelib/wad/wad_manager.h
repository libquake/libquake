#pragma once

#include <map>
#include <memory>
#include <quakelib/wad/wad.h>
#include <string>
#include <vector>

namespace quakelib::wad {

  /**
   * @brief Manages multiple WAD files and provides unified texture lookup.
   *
   * This class allows loading multiple WAD files (e.g., from a map's wad list)
   * and searching for a texture across all of them in order.
   */
  class QuakeWadManager {
  public:
    /**
     * @brief Default constructor.
     */
    QuakeWadManager();

    /**
     * @brief Destructor.
     */
    ~QuakeWadManager();

    /**
     * @brief Adds a WAD file to the manager. A file that is already loaded is skipped.
     * @param path The file path to the .wad file.
     * @return true if the file was loaded now.
     */
    bool AddWadFile(const std::string &path);

    /**
     * @brief Adds every .wad file of a folder (not recursive), in name order, after the ones already loaded.
     * @return number of files loaded now.
     */
    size_t AddWadDirectory(const std::string &dir);

    /**
     * @brief Looks for a wad by file name in the given folders, case insensitive (maps often say
     * "START.WAD" or "gfx/base.wad"). Returns the full path or an empty string.
     */
    static std::string FindWadFile(const std::string &name, const std::vector<std::string> &dirs);

    /// paths of the loaded wads, in search order
    const std::vector<std::string> &LoadedFiles() const { return files; }

    /**
     * @brief Finds a texture by name across all loaded WADs.
     *
     * Search order is typically the order in which WADs were added.
     *
     * @param name The name of the texture to find.
     * @return Pointer to the texture if found, nullptr otherwise.
     */
    QuakeTexture *FindTexture(const std::string &name);

  private:
    std::vector<QuakeWadPtr> wads; ///< List of managed WAD files.
    std::vector<std::string> files; ///< canonical paths, same order as wads
  };

} // namespace quakelib::wad
