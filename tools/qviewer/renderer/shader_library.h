#pragma once

#include <raylib.h>

/**
 * @brief Loads the viewer's shaders by file name, e.g. LoadViewerShader("world.vert", "world.frag").
 *
 * The copies embedded at build time are used unless fromDisk is set, then the files are read from the source
 * folder (hot reload). A null vertex shader uses raylib's default, which fullscreen passes rely on.
 * defines (e.g. "#define ALPHA_TEST\n") go right after the #version line of the fragment shader.
 * Returns a shader with id 0 if the code does not compile.
 */
Shader LoadViewerShader(const char *vertexName, const char *fragmentName, bool fromDisk,
                        const char *defines = nullptr);

/// the shaders brush surfaces are drawn with, picked per texture
struct SurfaceShaders {
  Shader world{}; ///< lit walls
  Shader alpha{}; ///< lit walls with holes ('{' textures), world.frag with ALPHA_TEST
  Shader sky{};
};
