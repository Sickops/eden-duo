#!/bin/sh
# build_slice.sh: Linux (and optionally Android) module build + strip + package for the D3 live slice.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd); B=$R/../$(basename "$R")-build
if [ -z "$EDEN_SOURCE_ROOT" ]; then
  echo "build_slice.sh: EDEN_SOURCE_ROOT must be set (an Eden Duo checkout, runtime 22 / ABI 1)" >&2
  exit 1
fi
EDEN=$EDEN_SOURCE_ROOT
cmake -S "$R/native/modules" -B "$B/linux" -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDEN_SOURCE_ROOT="$EDEN" >/dev/null
cmake --build "$B/linux" --target dsmod-d3 dsmod-d3-tool
strip --strip-all -o "$B/01001B300B9BE000.linux.so" "$B/linux/01001B300B9BE000.so"
MODS="--module linux-x86_64=$B/01001B300B9BE000.linux.so"
if [ -n "$ANDROID" ]; then
  if [ -n "$ANDROID_NDK_ROOT" ]; then
    NDK=$ANDROID_NDK_ROOT
  elif [ -n "$ANDROID_HOME" ]; then
    NDK=$(ls -d "$ANDROID_HOME"/ndk/*/ 2>/dev/null | head -1); NDK=${NDK%/}
  fi
  if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "build_slice.sh: ANDROID=1 needs ANDROID_NDK_ROOT (a specific NDK dir) or ANDROID_HOME (an SDK root with ndk/<version>/) set" >&2
    exit 1
  fi
  cmake -S "$R/native/modules" -B "$B/android" -G Ninja -DEDEN_SOURCE_ROOT="$EDEN" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$B/android" --target dsmod-d3
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-all -o "$B/01001B300B9BE000.android.so" "$B/android/01001B300B9BE000.so"
  MODS="$MODS --module android-arm64-v8a=$B/01001B300B9BE000.android.so"
fi
python3 "$R/tools/d3/gen_manifest.py"
# build_ids: read back out of the manifest gen_manifest.py just wrote (itself parsed from
# native/modules/d3_build.cpp's own Build entries), the same technique tools/build_release.sh's
# build_ids() helper uses for every other companion -- never hand-typed here.
BUILD_IDS=$(python3 -c 'import json,sys; [print("--build-id", b) for b in json.load(open(sys.argv[1]))["module"]["build_ids"]]' \
  "$R/packages/DiabloIII/dualscreen/manifest.json")
python3 "$R/tools/build_dualscreen_package.py" --package "$R/packages/DiabloIII" --output "$B/dist" \
  --title-id 01001B300B9BE000 --version 1.0.0 $MODS --abi 1 $BUILD_IDS --min-runtime 22
python3 "$R/tools/compact_zip.py" "$B/dist/01001B300B9BE000.dsmod.zip" >/dev/null
sha256sum "$B/dist/01001B300B9BE000.dsmod.zip" "$B"/01001B300B9BE000.*.so
