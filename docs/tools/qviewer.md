# qviewer

A viewer for `.map` and `.bsp` files built on the Level API and raylib. It is also a working example of
everything on these pages: wad lookup, baking, light styles, fullbrights and sky.

```sh
qviewer -m maps/start.map -w wads/
qviewer -m maps/e1m1.bsp
```

`.map` files are baked on load. Light styles animate as in Quake.

## Options

| Option | |
|---|---|
| `-m file` | map or bsp to open |
| `-w path` | wad folder or file, can be repeated. Wads named by the map are used first, everything else fills the gaps |
| `--no-bake` | skip baking for `.map` files |
| `--fullbright` | start without lightmap |
| `--wireframe` | start with the line overlay |
| `--fog` | fog even if the map has no `fog` key |
| `--no-bloom` | start without bloom |
| `--lights-off` | start with switchable lights off |
| `--screenshot file` | save a screenshot and quit |
| `--frames n` | frames before the screenshot, default 30 |
| `--style-time s` | freeze the light styles at this time |

## Keys

| Key | |
|---|---|
| WASD, Space, Ctrl | move, Shift is faster, the wheel changes the speed |
| M | capture the mouse |
| R | reset the camera |
| P | pause light styles |
| T | switchable lights on / off |
| B | fullbright |
| L | line overlay |
| F, `[`, `]` | fog, fog density |
| O, `-`, `=` | bloom, bloom strength |
| X | point filtering |
| F5 | reload shaders |

## Shaders

The GLSL files in `tools/qviewer/shaders/` are compiled into the binary. In a build from source they are
also read from the source folder on F5, so changes show up without a restart.

| File | |
|---|---|
| `world.vert` / `world.frag` | lightmap, fullbright pixels, fog. Compiled a second time with `ALPHA_TEST` for `{` textures |
| `sky.frag` | Quake's two layer scrolling sky |
| `bloom_extract.frag`, `bloom_blur.frag`, `bloom_composite.frag` | bloom on fullbright pixels and overbright light |

Fog follows FitzQuake and reads the worldspawn keys `fog` (`"density r g b"`) and `skyfog`.
