# Embree

[Embree](https://github.com/RenderKit/embree) 4.4.1, static, for the light baker's rays
(`src/common/bake_bvh_embree.cpp`). Apache 2.0, see `LICENSE.txt`.

| Folder | |
|---|---|
| `include/embree4` | headers, the same for every platform |
| `macos` | universal: NEON2X on arm64, SSE4.2 on x86_64 |

Every platform folder has `libembree4.a` and `libembree4_isa.a`, the kernels per ISA. They are separate
archives because the core holds the same files built for the base ISA.

`cmake/embree.cmake` links the folder of the platform when it exists. Platforms without one, and builds with
`QLIB_EMBREE=OFF`, use libquake's own bvh.

## Building

```sh
third_party/embree/build.sh
```

builds for the platform it runs on (macOS or Linux) and replaces its folder, the headers and the license.
It fetches the source of the version in the script and checks its hash. Embree is built with what the baker
needs only: triangles, single rays, its own threads instead of TBB.

To move to another Embree version, change `VERSION` and `SHA256` in the script and run it on every platform.
