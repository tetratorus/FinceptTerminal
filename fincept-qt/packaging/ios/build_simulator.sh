#!/usr/bin/env bash
# Build Fincept Terminal for the arm64 iOS Simulator. See docs/IOS.md.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"   # fincept-qt/
qt="${QT_ROOT:-$here/../.qt/6.8.3}"            # aqt layout: ios/ + macos/
ssl="${OPENSSL_IOS_SIM:?set OPENSSL_IOS_SIM to an OpenSSL built for iossimulator-arm64}"
build="$here/build/ios-simulator"

# Qt's prebuilt iOS kit only has an x86_64 simulator slice, which iOS 26+
# simulators on Apple Silicon can't run: make a retargeted APFS clone once.
if [ ! -d "$qt/ios-sim" ]; then
    cp -Rc "$qt/ios" "$qt/ios-sim"
    python3 "$here/packaging/ios/retarget_qt_arm64_simulator.py" "$qt/ios-sim"
fi

"$qt/ios-sim/bin/qt-cmake" -S "$here" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DQT_HOST_PATH="$qt/macos" \
    -DOPENSSL_INCLUDE_DIR="$ssl/include" \
    -DOPENSSL_CRYPTO_LIBRARY="$ssl/lib/libcrypto.a" \
    -DOPENSSL_SSL_LIBRARY="$ssl/lib/libssl.a" \
    -DFINCEPT_DOWNLOAD_YTDLP=OFF
cmake --build "$build"
codesign --force --sign - "$build/FinceptTerminal.app"
echo "App bundle: $build/FinceptTerminal.app"
