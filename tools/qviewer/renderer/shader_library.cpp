#include "shader_library.h"

#include <cstring>
#include <embedded_shaders.h>
#include <rlgl.h>
#include <string>

namespace {
  std::string sourceOf(const char *name, bool fromDisk) {
    if (!name)
      return {};
    if (fromDisk) {
      std::string path = std::string(QVIEWER_SHADER_DIR) + "/" + name;
      if (char *text = LoadFileText(path.c_str())) {
        std::string code = text;
        UnloadFileText(text);
        return code;
      }
      TraceLog(LOG_WARNING, "shader %s not found, using the embedded copy", path.c_str());
    }
    for (const auto &s : EMBEDDED_SHADERS) {
      if (std::strcmp(s.name, name) == 0)
        return s.code;
    }
    TraceLog(LOG_ERROR, "unknown shader %s", name);
    return {};
  }
} // namespace

Shader LoadViewerShader(const char *vertexName, const char *fragmentName, bool fromDisk) {
  std::string vs = sourceOf(vertexName, fromDisk);
  std::string fs = sourceOf(fragmentName, fromDisk);
  Shader shader = LoadShaderFromMemory(vs.empty() ? nullptr : vs.c_str(), fs.c_str());
  // raylib falls back to its default shader (and its shared locations) when compiling fails
  if (shader.id == rlGetShaderIdDefault()) {
    TraceLog(LOG_ERROR, "shader %s / %s did not compile", vertexName ? vertexName : "default", fragmentName);
    shader = Shader{};
  }
  return shader;
}
