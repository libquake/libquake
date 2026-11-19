#!/bin/bash
# Builds the static Embree that libquake's baker links, for the platform this runs on, and puts it next to
# this script: include/embree4 and <platform>/libembree4.a. Needs curl, cmake and a C++ compiler, ninja is
# used when installed.
set -euo pipefail

VERSION=4.4.1
SHA256=dcf338cc61b636c871ccf370e673bfd380c5ecb71ce49ad50f28e1d4ec9995dc

here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL "https://github.com/RenderKit/embree/archive/refs/tags/v$VERSION.tar.gz" -o "$work/embree.tar.gz"
echo "$SHA256  $work/embree.tar.gz" | shasum -a 256 -c - > /dev/null
tar xzf "$work/embree.tar.gz" -C "$work"
src="$work/embree-$VERSION"

# only what the baker needs: triangles, single rays, Embree's own threads instead of TBB
options=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DEMBREE_STATIC_LIB=ON
  -DEMBREE_TASKING_SYSTEM=INTERNAL
  -DEMBREE_TUTORIALS=OFF
  -DEMBREE_ISPC_SUPPORT=OFF
  -DEMBREE_ZIP_MODE=OFF
  -DEMBREE_GEOMETRY_QUAD=OFF
  -DEMBREE_GEOMETRY_CURVE=OFF
  -DEMBREE_GEOMETRY_SUBDIVISION=OFF
  -DEMBREE_GEOMETRY_USER=OFF
  -DEMBREE_GEOMETRY_INSTANCE=OFF
  -DEMBREE_GEOMETRY_INSTANCE_ARRAY=OFF
  -DEMBREE_GEOMETRY_GRID=OFF
  -DEMBREE_GEOMETRY_POINT=OFF
  -DEMBREE_RAY_PACKETS=OFF
  -DEMBREE_RAY_MASK=OFF
  -DEMBREE_FILTER_FUNCTION=OFF
)
if command -v ninja > /dev/null; then
  options+=(-G Ninja)
fi

# build <name> [cmake args]: installs to $work/install-<name>
build() {
  local name=$1
  shift
  echo "building Embree $VERSION ($name)"
  if ! { cmake -S "$src" -B "$work/build-$name" "${options[@]}" -DCMAKE_INSTALL_PREFIX="$work/install-$name" "$@" &&
    cmake --build "$work/build-$name" --parallel &&
    cmake --install "$work/build-$name"; } > "$work/$name.log" 2>&1; then
    tail -40 "$work/$name.log"
    exit 1
  fi
}

# merge <output> <archives...>
merge() {
  local out=$1
  shift
  if [[ $(uname -s) == Darwin ]]; then
    /usr/bin/libtool -static -no_warning_for_no_symbols -o "$out" "$@"
  else
    {
      echo "CREATE $out"
      for lib in "$@"; do
        echo "ADDLIB $lib"
      done
      echo "SAVE"
      echo "END"
    } | ar -M
  fi
}

# pack <name>: $work/<name>.a from Embree's core archives, $work/<name>_isa.a from its per ISA archives. They
# stay apart, the core holds the same files built for the base ISA.
pack() {
  local core=() isa=()
  for lib in "$work/install-$1"/lib/*.a; do
    case $(basename "$lib") in
    libembree_*) isa+=("$lib") ;;
    *) core+=("$lib") ;;
    esac
  done
  merge "$work/$1.a" "${core[@]}"
  if ((${#isa[@]})); then
    merge "$work/$1_isa.a" "${isa[@]}"
  fi
}

case "$(uname -s)" in
Darwin)
  # AppleClang allows one ISA for a static Embree: Apple's default NEON2X on arm64, SSE4.2 on x86_64
  build arm64 -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
  build x86_64 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DEMBREE_ISA_SSE2=OFF \
    -DEMBREE_ISA_SSE42=ON -DEMBREE_ISA_AVX=OFF -DEMBREE_ISA_AVX2=OFF -DEMBREE_ISA_AVX512=OFF
  pack arm64
  pack x86_64
  platform=macos
  rm -rf "${here:?}/$platform"
  mkdir -p "$here/$platform"
  lipo -create "$work/arm64.a" "$work/x86_64.a" -output "$here/$platform/libembree4.a"
  lipo -create "$work/arm64_isa.a" "$work/x86_64_isa.a" -output "$here/$platform/libembree4_isa.a"
  ;;
Linux)
  arch=$(uname -m)
  case "$arch" in
  x86_64) build "$arch" -DEMBREE_ISA_AVX=OFF -DEMBREE_ISA_AVX512=OFF ;;
  *) build "$arch" ;;
  esac
  pack "$arch"
  platform=linux-$arch
  rm -rf "${here:?}/$platform"
  mkdir -p "$here/$platform"
  cp "$work/$arch.a" "$here/$platform/libembree4.a"
  if [[ -f "$work/${arch}_isa.a" ]]; then
    cp "$work/${arch}_isa.a" "$here/$platform/libembree4_isa.a"
  fi
  ;;
*)
  echo "no build for $(uname -s) yet" >&2
  exit 1
  ;;
esac

rm -rf "$here/include"
mkdir -p "$here/include/embree4"
first=$(ls -d "$work"/install-* | head -1)
cp "$first"/include/embree4/*.h "$here/include/embree4/"
cp "$src/LICENSE.txt" "$here/LICENSE.txt"
echo "done: $here/$platform"
