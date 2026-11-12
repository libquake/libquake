# Wad

Namespace `quakelib::wad`.

## QuakeWadManager

`#include <quakelib/wad/wad_manager.h>`

| Member | |
|---|---|
| `bool AddWadFile(const std::string &path)` | false if it could not be loaded or is already loaded |
| `size_t AddWadDirectory(const std::string &dir)` | every `.wad` in the folder, sorted by name, not recursive. Returns the number loaded |
| `static std::string FindWadFile(const std::string &name, const std::vector<std::string> &dirs)` | path of a wad by file name, case insensitive, empty if not found |
| `const std::vector<std::string> &LoadedFiles() const` | in search order |
| `QuakeTexture *FindTexture(const std::string &name)` | first match in search order, `nullptr` if missing |

Textures are decoded on first access. The pointers stay valid as long as the manager lives.

## QuakeWad

`#include <quakelib/wad/wad.h>`

| Member | |
|---|---|
| `static QuakeWadPtr FromFile(const std::string &path, QuakeWadOptions opts = {})` | `nullptr` if the file cannot be opened, throws if it is not a WAD2 |
| `QuakeTexture *GetTexture(const std::string &name)` | case insensitive, an exact match wins |
| `const std::map<std::string, QuakeWadEntry> &Textures()` | all entries, pixels only after `GetTexture` |
| `void SetPalette(const Palette &)` | default is the Quake palette |
| `static bool IsSkyTexture(const std::string &name)` | name starts with `sky` |

`QuakeWadOptions::flipTexHorizontal` stores the rows bottom up.

## QuakeTexture

| Member | |
|---|---|
| `unsigned int width`, `height` | |
| `cvec raw` | RGBA pixels, `color::rgba[4]` each. Sky: the back layer |
| `bool hasEmission` | has fullbright pixels |
| `bool hasAlpha` | name starts with `{`, index 255 is transparent |
| `cvec emission` | black except the fullbright pixels, empty without them |
| `cvec front` | sky only: the cloud layer, alpha 0 where the back layer shows |
| `QuakeTextureType type` | `TTYPE_SKY_TEXTURE` for skies |

## Palette

`#include <quakelib/wad/palette.h>`

| Member | |
|---|---|
| `static Palette FromFile(const std::string &path)` | a `palette.lmp` |
| `static Palette FromBuffer(const unsigned char *data, int size)` | 768 bytes RGB |
| `const color GetColor(int index) const` | all colors opaque |
| `static bool IsFullbright(int index)` | 224..254 |
