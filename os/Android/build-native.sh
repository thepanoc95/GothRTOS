#!/bin/sh
# build-native.sh -- build gothdroid and libgothgfx.so with the Android NDK.
#
# The Gradle project stages everything under os/Android/prebuilt/<abi>/ into
# the APK assets.  Run this script before assembling the app:
#
#     os/Android/build-native.sh
#     cd os/Android/gothdroid && ./gradlew assembleDebug
#
# Environment:
#   ANDROID_NDK_HOME   NDK path (otherwise discovered under the Android SDK)
#   GOTHDROID_ABIS     space separated ABI list (default: all four)
#   CMAKE              cmake executable (default: cmake)
#
# The result is:
#   prebuilt/<abi>/gothdroid
#   prebuilt/<abi>/libgothgfx.so
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD="$HERE/build/native"
OUT="$HERE/prebuilt"

find_ndk() {
    if [ -n "${ANDROID_NDK_HOME:-}" ]; then
        printf '%s\n' "$ANDROID_NDK_HOME"
        return 0
    fi
    if [ -n "${ANDROID_NDK_ROOT:-}" ]; then
        printf '%s\n' "$ANDROID_NDK_ROOT"
        return 0
    fi
    for base in "${ANDROID_SDK_ROOT:-}" "${ANDROID_HOME:-}" "$HOME/Android/Sdk"; do
        [ -n "$base" ] || continue
        [ -d "$base/ndk" ] || continue
        candidate=$(ls -1 "$base/ndk" 2>/dev/null | sort -V | tail -n 1)
        if [ -n "$candidate" ] && [ -d "$base/ndk/$candidate" ]; then
            printf '%s\n' "$base/ndk/$candidate"
            return 0
        fi
    done
    return 1
}

NDK=$(find_ndk || true)
if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
    echo "build-native.sh: Android NDK not found." >&2
    echo "Set ANDROID_NDK_HOME or install an NDK under \$ANDROID_HOME/ndk." >&2
    exit 1
fi

CMAKE=${CMAKE:-cmake}
ABIS=${GOTHDROID_ABIS:-"arm64-v8a armeabi-v7a x86_64 x86"}

echo "NDK:   $NDK"
echo "CMake: $($CMAKE --version | head -n 1)"
echo "ABIs:  $ABIS"

for abi in $ABIS; do
    echo
    echo "==> $abi"
    "$CMAKE" -S "$HERE" -B "$BUILD/$abi" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-29 \
        -DCMAKE_BUILD_TYPE=Release
    "$CMAKE" --build "$BUILD/$abi" --target gothdroid gothgfx --parallel

    mkdir -p "$OUT/$abi"
    cp "$BUILD/$abi/gothdroid" "$OUT/$abi/gothdroid"
    chmod 755 "$OUT/$abi/gothdroid"
    if [ -f "$BUILD/$abi/libgothgfx.so" ]; then
        cp "$BUILD/$abi/libgothgfx.so" "$OUT/$abi/libgothgfx.so"
    fi
    echo "staged $OUT/$abi"
done

echo
echo "Done. Rebuild the APK so Gradle stages os/Android/prebuilt into assets."
