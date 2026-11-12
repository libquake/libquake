# Textures

## Wads

`QuakeWadManager` loads wad files and finds textures by name across all of them, first match wins.

Maps name their wads in the worldspawn, often with paths from the mapper's machine (`"wad"
"C:/quake/id1/gfx/START.WAD"`). `level.wads` and `doc.wads` hold the file names without
path. `FindWadFile` looks them up in your folders, case insensitive:

```cpp
--8<-- "examples/wad_textures.cpp:lookup"
```

Adding the whole folder after the named wads covers maps that ask for wads you don't have, as long as
another wad contains the textures. `AddWadFile` skips files that are already loaded, so the order above
keeps the named wads first.

## Pixels

`QuakeTexture::raw` holds `width * height` RGBA colors converted with the Quake palette, top row first.
Textures whose name starts with `{` have `hasAlpha` set and palette index 255 is transparent (all four
channels 0), the same rule as the engine. Everywhere else index 255 is an ordinary opaque color. Bsp
textures (`TextureInfo`) follow the same rule. `QuakeWad::FromFile` with
`flipTexHorizontal = true` stores the rows bottom up instead (the name is misleading, it flips vertically).

## Emission

Palette indices 224..254 are fullbright: Quake draws them at full brightness regardless of the
lightmap. Textures that use them have `hasEmission` set and an `emission` image of the same size, black
except for the fullbright pixels. This holds for wad textures (`QuakeTexture`) and bsp textures
(`TextureInfo`). Sky textures never have emission.

```cpp
--8<-- "examples/wad_textures.cpp:emission"
```

To match Quake, show the emission instead of the lit color where a pixel is fullbright. With filtering,
the ratio of emission to albedo gives a smooth transition:

```glsl
vec3 emission = texture(emissionMap, uv).rgb;
float glow = clamp(max3(emission) / max(max3(albedo.rgb), 0.001), 0.0, 1.0);
vec3 color = mix(albedo.rgb * light, emission, glow);
```

`glow` is also a good bloom mask, see the qviewer shaders.

## Sky

Quake skies are 256x128 with two layers side by side. Wad sky textures (names starting with `sky`) are
split on load: `raw` is the solid back layer, `front` the cloud layer with palette index 0 transparent,
both 128x128. Bsp sky textures in `level.textures` are split the same way: `rgba` is the back layer,
`skyFront` the cloud layer, `width` is the layer width. Mesh texture coordinates still use the full
miptex width (`RenderMesh::textureWidth`).
