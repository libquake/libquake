#include "bloom.h"
#include "shader_library.h"

#include <initializer_list>
#include <rlgl.h>

bool Bloom::LoadShaders(bool fromDisk) {
  Shader extract = LoadViewerShader(nullptr, "bloom_extract.frag", fromDisk);
  Shader blur = LoadViewerShader(nullptr, "bloom_blur.frag", fromDisk);
  Shader composite = LoadViewerShader(nullptr, "bloom_composite.frag", fromDisk);
  if (extract.id == 0 || blur.id == 0 || composite.id == 0) {
    for (Shader s : {extract, blur, composite})
      if (s.id > 0)
        UnloadShader(s);
    return false;
  }

  for (Shader s : {extractShader, blurShader, compositeShader})
    if (s.id > 0)
      UnloadShader(s);
  extractShader = extract;
  blurShader = blur;
  compositeShader = composite;
  blurDirectionLoc = GetShaderLocation(blurShader, "direction");
  bloomSmallLoc = GetShaderLocation(compositeShader, "bloomSmall");
  bloomLargeLoc = GetShaderLocation(compositeShader, "bloomLarge");
  strengthLoc = GetShaderLocation(compositeShader, "bloomStrength");
  return true;
}

void Bloom::Unload() {
  for (Shader s : {extractShader, blurShader, compositeShader})
    if (s.id > 0)
      UnloadShader(s);
  extractShader = blurShader = compositeShader = Shader{};
  resize(0, 0);
}

void Bloom::resize(int w, int h) {
  if (w == width && h == height)
    return;
  if (scene.id > 0)
    UnloadRenderTexture(scene);
  for (auto &level : levels) {
    if (level.ping.id > 0)
      UnloadRenderTexture(level.ping);
    if (level.pong.id > 0)
      UnloadRenderTexture(level.pong);
    level = Level{};
  }
  scene = RenderTexture2D{};
  width = w;
  height = h;
  if (w <= 0 || h <= 0)
    return;

  scene = LoadRenderTexture(w, h);
  int divisor = 2;
  for (auto &level : levels) {
    int lw = w / divisor > 0 ? w / divisor : 1;
    int lh = h / divisor > 0 ? h / divisor : 1;
    level.ping = LoadRenderTexture(lw, lh);
    level.pong = LoadRenderTexture(lw, lh);
    // bilinear filtering does the downsampling and halves the blur taps
    SetTextureFilter(level.ping.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(level.pong.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(level.ping.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(level.pong.texture, TEXTURE_WRAP_CLAMP);
    divisor *= 2;
  }
  SetTextureFilter(scene.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(scene.texture, TEXTURE_WRAP_CLAMP);
}

void Bloom::BeginScene(Color background) {
  resize(GetScreenWidth(), GetScreenHeight());
  BeginTextureMode(scene);
  // alpha 0: the background does not glow
  background.a = 0;
  ClearBackground(background);
}

void Bloom::EndScene() { EndTextureMode(); }

void Bloom::drawFullscreen(Texture2D source, int w, int h) {
  // render textures are stored upside down, the negative height flips them back
  DrawTexturePro(source, Rectangle{0, 0, (float)source.width, -(float)source.height},
                 Rectangle{0, 0, (float)w, (float)h}, Vector2{0, 0}, 0.0f, WHITE);
}

void Bloom::blur(Level &level) {
  const int w = level.ping.texture.width, h = level.ping.texture.height;
  Vector2 horizontal = {1.0f / w, 0.0f};
  Vector2 vertical = {0.0f, 1.0f / h};

  BeginShaderMode(blurShader);
  SetShaderValue(blurShader, blurDirectionLoc, &horizontal, SHADER_UNIFORM_VEC2);
  BeginTextureMode(level.pong);
  drawFullscreen(level.ping.texture, w, h);
  EndTextureMode();

  SetShaderValue(blurShader, blurDirectionLoc, &vertical, SHADER_UNIFORM_VEC2);
  BeginTextureMode(level.ping);
  drawFullscreen(level.pong.texture, w, h);
  EndTextureMode();
  EndShaderMode();
}

void Bloom::Composite(float strength) {
  if (scene.id == 0)
    return;

  // 1. extract what glows into the half resolution level
  Level &small = levels[0];
  BeginTextureMode(small.ping);
  ClearBackground(BLACK);
  BeginShaderMode(extractShader);
  drawFullscreen(scene.texture, small.ping.texture.width, small.ping.texture.height);
  EndShaderMode();
  EndTextureMode();
  blur(small);

  // 2. the quarter resolution level starts from the blurred half resolution one, for a wider glow
  Level &large = levels[1];
  BeginTextureMode(large.ping);
  drawFullscreen(small.ping.texture, large.ping.texture.width, large.ping.texture.height);
  EndTextureMode();
  blur(large);

  // 3. scene + glow to the screen
  BeginShaderMode(compositeShader);
  SetShaderValueTexture(compositeShader, bloomSmallLoc, small.ping.texture);
  SetShaderValueTexture(compositeShader, bloomLargeLoc, large.ping.texture);
  SetShaderValue(compositeShader, strengthLoc, &strength, SHADER_UNIFORM_FLOAT);
  drawFullscreen(scene.texture, GetScreenWidth(), GetScreenHeight());
  EndShaderMode();
}
