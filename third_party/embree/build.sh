#!/bin/bash
# Builds the static Embree that libquake's baker links, for the platform this runs on, and puts it next to
# this script: include/embree4 and <platform>/libembree4.a (embree4.lib on Windows). Needs curl, cmake and a
# C++ compiler, ninja is used when installed. On Windows it runs in Git Bash with Visual Studio and needs
# llvm-lib, which comes with Visual Studio's clang tools or LLVM.
set -euo pipefail

VERSION=4.4.1
SHA256=dcf338cc61b636c871ccf370e673bfd380c5ecb71ce49ad50f28e1d4ec9995dc

here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL "https://github.com/RenderKit/embree/archive/refs/tags/v$VERSION.tar.gz" -o "$work/embree.tar.gz"
echo "$SHA256  $work/embree.tar.gz" | sha256sum -c - > /dev/null 2>&1 ||
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
windows=false
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*) windows=true ;;
esac
# Visual Studio's generator finds the compiler by itself, ninja would need its environment
if ! $windows && command -v ninja > /dev/null; then
  options+=(-G Ninja)
fi

# build <name> [cmake args]: installs to $work/install-<name>
build() {
  local name=$1
  shift
  echo "building Embree $VERSION ($name)"
  if ! { cmake -S "$src" -B "$work/build-$name" "${options[@]}" -DCMAKE_INSTALL_PREFIX="$work/install-$name" "$@" &&
    cmake --build "$work/build-$name" --config Release --parallel &&
    cmake --install "$work/build-$name" --config Release; } > "$work/$name.log" 2>&1; then
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
  elif $windows; then
    local libtool
    libtool=$(command -v llvm-lib || echo "/c/Program Files/LLVM/bin/llvm-lib.exe")
    "$libtool" -nologo -out:"$(cygpath -w "$out")" $(for lib in "$@"; do cygpath -w "$lib"; done)
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

# pack <name> [extension]: $work/<name>.<extension> from Embree's core archives, $work/<name>_isa.<extension>
# from its per ISA archives. They stay apart, the core holds the same files built for the base ISA.
pack() {
  local ext=${2:-a} core=() isa=()
  for lib in "$work/install-$1"/lib/*."$ext"; do
    case $(basename "$lib") in
    libembree_* | embree_*) isa+=("$lib") ;;
    *) core+=("$lib") ;;
    esac
  done
  merge "$work/$1.$ext" "${core[@]}"
  if ((${#isa[@]})); then
    merge "$work/$1_isa.$ext" "${isa[@]}"
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
  if ! $windows; then
    echo "no build for $(uname -s) yet" >&2
    exit 1
  fi
  # libquake's dll shares the C runtime dll with its users, the Godot extension links the runtime statically:
  # one build for each, both with SSE2, SSE4.2 and AVX2 kernels
  isas=(-DEMBREE_ISA_AVX=OFF -DEMBREE_ISA_AVX512=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW)
  build dll "${isas[@]}" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
  build static "${isas[@]}" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
  pack dll lib
  pack static lib
  platform=windows-x64
  for variant in "$platform" "$platform-static-runtime"; do
    rm -rf "${here:?}/$variant"
    mkdir -p "$here/$variant"
  done
  cp "$work/dll.lib" "$here/$platform/embree4.lib"
  cp "$work/dll_isa.lib" "$here/$platform/embree4_isa.lib"
  cp "$work/static.lib" "$here/$platform-static-runtime/embree4.lib"
  cp "$work/static_isa.lib" "$here/$platform-static-runtime/embree4_isa.lib"
  ;;
esac

rm -rf "$here/include"
mkdir -p "$here/include/embree4"
first=$(ls -d "$work"/install-* | head -1)
cp "$first"/include/embree4/*.h "$here/include/embree4/"
cp "$src/LICENSE.txt" "$here/LICENSE.txt"
echo "done: $here/$platform"
