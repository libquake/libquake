#pragma once

#include "palette.h"
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace quakelib::wad {
  /// Maximum length of a texture name in bytes.
  static const int TEXTURE_NAME_LENGTH = 16;
  /// Number of mipmap levels typically stored in a texture.
  static const int MAX_MIP_LEVELS = 4;

  /**
   * @brief Enumeration of supported texture types.
   */
  enum QuakeTextureType { TTYPE_DEFAULT_TEXTURE = 0, TTYPE_SKY_TEXTURE = 1 };

  /**
   * @brief Represents a texture loaded from a WAD.
   *
   * Contains raw pixel data (converted to RGBA usually via Palette) and metadata.
   */
  struct QuakeTexture {
    /**
     * @brief Decodes raw WAD data into this texture structure.
     * @param buff Pointer to the raw data buffer.
     * @param size Size of the buffer.
     * @param flipHorizontal Whether to flip the texture horizontally.
     * @param pal The palette to use for color lookup.
     */
    virtual void FillTextureData(const uint8_t *buff, size_t size, bool flipHorizontal, const Palette &pal);

    unsigned int width = 0;                        ///< Width in pixels.
    unsigned int height = 0;                       ///< Height in pixels.
    unsigned int mipOffsets[MAX_MIP_LEVELS];       ///< Offsets to mipmap levels.
    bool hasEmission = false; ///< true if the texture has fullbright pixels (palette 224..254)
    /// same layout as raw: black everywhere except the fullbright pixels, empty without fullbrights
    cvec emission;
    /// sky textures only: the front (cloud) layer, palette index 0 is transparent. raw is the back layer.
    cvec front;
    QuakeTextureType type = TTYPE_DEFAULT_TEXTURE; ///< Type of the texture.
    cvec raw;                                      ///< Raw pixel data (usually RGBA).
  };

  /**
   * @brief Specialization for Sky textures (often split into halves).
   */
  struct QuakeSkyTexture : public QuakeTexture {
    /**
     * @brief Decodes sky texture data.
     * @param buff Pointer to the raw data.
     * @param size Size of the data.
     * @param flipHorizontal Flip flag.
     * @param pal Palette.
     */
    void FillTextureData(const uint8_t *buff, size_t size, bool flipHorizontal, const Palette &pal);

    /**
     * @brief Gets the base layer of the sky.
     * @return Reference to vector of colors.
     */
    const cvec &BaseSky() const;

    /**
     * @brief Gets the front layer of the sky, index 0 is transparent.
     * @return Reference to vector of colors.
     */
    const cvec &FrontSky() const;
  };

  /**
   * @brief Represents an entry in the WAD directory.
   */
  struct QuakeWadEntry {
    /**
     * @brief Types of entries found in WAD files.
     */
    enum QuakeWadEntryType {
      QWET_Palette = 0x40,     ///< Palette lump.
      QWET_SBarPic = 0x42,     ///< Status bar picture.
      QWET_MipsTexture = 0x44, ///< Mipmapped texture (standard wall).
      QWET_ConsolePic = 0x45   ///< Console picture.
    };

    /**
     * @brief Raw binary header of a WAD entry.
     */
    struct header {
      unsigned int offset;       ///< Offset in the file.
      unsigned int inWadSize;    ///< Size on disk (compressed).
      unsigned int size;         ///< Uncompressed size.
      unsigned char type;        ///< Entry type.
      unsigned char compression; ///< Compression flag.
      unsigned short unknown;    ///< Unknown padding/flag.
    } header;

    std::string name;     ///< Name of the entry.
    QuakeTexture texture; ///< The actual loaded texture data.

    /**
     * @brief Gets the type of this entry.
     * @return The QuakeWadEntryType.
     */
    QuakeWadEntryType Type() const { return (QuakeWadEntryType)header.type; }
  };

  class QuakeWad;
  /**
   * @brief Shared pointer to a QuakeWad.
   */
  using QuakeWadPtr = std::shared_ptr<QuakeWad>;

  /**
   * @brief Options for loading WAD files.
   */
  struct QuakeWadOptions {
    bool flipTexHorizontal; ///< If true, textures are flipped horizontally on load.
  };

  /**
   * @brief Represents a WAD file (texture archive).
   *
   * Handles loading WAD files, parsing the directory, and accessing textures.
   */
  class QuakeWad {
  public:
    QuakeWadOptions opts; ///< Options used for this WAD.

  public:
    /**
     * @brief Creates a QuakeWad instance from a file.
     * @param fileName Path to the .wad file.
     * @param opts Loading options.
     * @return A shared pointer to the loaded WAD, or nullptr on failure.
     */
    static QuakeWadPtr FromFile(const std::string &fileName, QuakeWadOptions opts = QuakeWadOptions());

    /**
     * @brief Creates a generic empty QuakeWad.
     * @return A shared pointer to the new WAD.
     */
    static QuakeWadPtr NewQuakeWad() { return std::make_shared<QuakeWad>(); }

    /**
     * @brief Creates a texture from a raw buffer (manual creation).
     * @param buff Raw pixel data.
     * @param isSky True if this is a sky texture.
     * @param width Width of the texture.
     * @param height Height of the texture.
     * @return Pointer to the created texture.
     */
    QuakeTexture *FromBuffer(const uint8_t *buff, bool isSky, int width, int height);

    /**
     * @brief Destructor.
     */
    ~QuakeWad();

    /**
     * @brief Retrieves a texture by name.
     * @param textureName Name of the texture.
     * @return Pointer to the texture if found, nullptr otherwise.
     */
    QuakeTexture *GetTexture(const std::string &textureName);

    /**
     * @brief Gets all texture entries in the WAD.
     * @return Map of names to WAD entries.
     */
    const std::map<std::string, QuakeWadEntry> &Textures() { return m_entries; };

    /**
     * @brief Sets the palette used for this WAD.
     * @param p The palette to copy.
     */
    void SetPalette(const Palette &p) { this->m_pal = p; };

    /**
     * @brief Gets current palette.
     * @return const reference to palette.
     */
    const Palette &GetPalette() { return m_pal; };

    /**
     * @brief Checks if a texture name corresponds to a sky texture convention.
     * @param texname The texture name (e.g., "sky1").
     * @return True if it starts with "sky", false otherwise.
     */
    static bool IsSkyTexture(const std::string texname);

  private:
    unsigned int m_numEntries;
    unsigned int m_dirOffset;
    Palette m_pal = default_palette;
    std::ifstream m_istream;
    std::map<std::string, QuakeWadEntry> m_entries;
  };
} // namespace quakelib::wad