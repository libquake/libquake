#pragma once

#include <raylib.h>

/**
 * @brief Loads the viewer's shaders by file name, e.g. LoadViewerShader("world.vert", "world.frag").
 *
 * The copies embedded at build time are used unless fromDisk is set, then the files are read from the source
 * folder (hot reload). A null vertex shader uses raylib's default, which fullscreen passes rely on.
 * Returns a shader with id 0 if the code does not compile.
 */
Shader LoadViewerShader(const char *vertexName, const char *fragmentName, bool fromDisk);
