# Lighting

## Lightmap layers

`level.lightmap` is an atlas split by light style. Every layer has the full atlas size, alpha 0 marks
texels the layer does not touch. Style 0 is static light, 1..31 are animated (flicker, pulse, ...), 32..62
are switchable lights created by the light tools from `targetname`.

The visible lightmap is the sum of all layers, each scaled by the current value of its style.
`LightStyleAnimator` does this on the CPU and gives you one RGBA image to upload.

128 is normal brightness. The shader multiplies by 2, as Quake does, so surfaces can get up to twice as
bright as their texture:

```glsl
vec3 albedo = texture(albedoMap, uv).rgb;
vec3 light = texture(lightmap, lightmapUV).rgb * 2.0;
vec3 color = albedo * light;
```

Quake computes this in gamma space. In a linear pipeline, convert the albedo to gamma before the multiply
and the result back to linear, otherwise brightness and contrast differ from Quake.

In a built map, faces without lightmap space (sky, liquids, tool textures) point at a single unlit texel
with value 128, so they show their texture unchanged.

## Light styles

```cpp
--8<-- "examples/light_styles.cpp:compose"
```

Patterns follow the Quake engine: one letter per tenth of a second, `a` is dark, `m` normal, `z` double.
The defaults are the ones from Quake's `world.qc`. Override any of them, for example to switch a light:

```cpp
--8<-- "examples/light_styles.cpp:switch"
```

Each frame, `Update` advances the patterns and `Recompose` rewrites only the texels of styles that changed:

```cpp
--8<-- "examples/light_styles.cpp:loop"
```

The light tools give every `targetname` of a light without `style` its own style from 32 up, in the order
the names first appear. libquake's baker does the same, `QuakeLightsFromEntities` returns the lights with
their assigned styles.

## Baking map lights

`BuildMap` with `bake = true` lights the map the way the Quake light tools do: direct light with shadows,
no bounce.

Light entities (`light`, `light_fluoro`, `light_torch_small_walltorch`, ... anything starting with
`light`) and their usual keys are read: `light`, `_color`, `wait`, `delay`, `style`, `targetname`,
`target` and `mangle` for spot lights, `angle` and `_softangle`. From the worldspawn: `light` /
`_minlight`, `_minlight_color`, `_anglescale`, `_range`, `_sunlight`, `_sun_mangle` and
`_sunlight_color`.

The world, `func_group`, `func_detail` and entities with `"_shadow" "1"` cast shadows. Sky faces block
light. Sunlight reaches a luxel through sky or by leaving the map, so maps without sky brushes get sun
too.

Additional lights and occluders are given in Quake coordinates, even when `convertToOpenGL` is set:

```cpp
--8<-- "examples/bake.cpp:extra"
```

`bakeOptions` replaces the settings from the worldspawn completely. To change one value, read them
first:

```cpp
--8<-- "examples/bake.cpp:settings"
```

Baking uses all hardware threads unless `BakeOptions::threads` says otherwise. Use a release build, a debug
build is many times slower.

## Lightmap uvs

`MapBuildOptions::lightmapUV` decides how `lightmap_uv` is made.

**Planar** (default) works like Quake: every connected region of coplanar faces gets a chart on a world
aligned grid, `luxelSize` units per texel (16 in Quake). All entities share one atlas. Only planar
layouts can be baked.

**XAtlas** unwraps each entity separately with [xatlas](https://github.com/jpcy/xatlas), for engines that
bake lighting themselves (Unity, Godot). `texelsPerUnit` and `resolution` control the density.

Pass a `LightmapLayout*` to `BuildMap` to get the planar charts. Each chart has a `key`, a hash of its
plane and triangles, so a tool can see which parts of a lightmap need to be rebaked after an edit.
