# Embree

[Embree](https://github.com/RenderKit/embree) 4.4.1, static, for the light baker's rays
(`src/common/bake_bvh_embree.cpp`). Apache 2.0, see `LICENSE.txt`.

| Folder | |
|---|---|
| `include/embree4` | headers, the same for every platform |
| `macos` | universal: NEON2X on arm64, SSE4.2 on x86_64 |
| `windows-x64` | SSE2, SSE4.2 and AVX2, for builds with the C runtime dll (`/MD`), like libquake's dll |
| `windows-x64-static-runtime` | the same for builds that link the runtime statically (`/MT`), like the Godot extension; `QLIB_EMBREE_STATIC_RUNTIME=ON` picks it |

Every platform folder has `libembree4.a` and `libembree4_isa.a` (`embree4.lib` and `embree4_isa.lib` on
Windows), the kernels per ISA. They are separate
archives because the core holds the same files built for the base ISA.

`cmake/embree.cmake` links the folder of the platform when it exists. Platforms without one, and builds with
`QLIB_EMBREE=OFF`, use libquake's own bvh. So do debug builds with MSVC: Embree is built for Release, and
MSVC's debug builds check other runtime settings.

## Building

```sh
third_party/embree/build.sh
```

builds for the platform it runs on (macOS or Linux, Windows in Git Bash with Visual Studio) and replaces its
folder, the headers and the license. The Windows folders come from the `embree` workflow, started by hand on
GitHub: it builds both, tests libquake with them and leaves them as the artifact `embree-windows`.
It fetches the source of the version in the script and checks its hash. Embree is built with what the baker
needs only: triangles, single rays, its own threads instead of TBB.

To move to another Embree version, change `VERSION` and `SHA256` in the script and run it on every platform.
