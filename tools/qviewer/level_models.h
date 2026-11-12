#pragma once

#include <quakelib/level.h>
#include <raylib.h>
#include <vector>

/**
 * @brief One raylib model per drawn brush entity of a level in opengl coordinates.
 *
 * Every mesh uses the material with the index of its texture in Level::textures. Meshes are split to fit
 * raylib's 16 bit indices. Trigger brushes and clip / skip / nodraw surfaces are left out.
 */
std::vector<Model> BuildLevelModels(const quakelib::Level &level);

/// meshes only, the materials belong to LevelTextures
void UnloadLevelModels(std::vector<Model> &models);
