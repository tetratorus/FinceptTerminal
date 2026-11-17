# iOS Simulator build (experimental)

There is no official mobile build. This builds the desktop UI for an arm64 iPhone
Simulator so the login, News and Notes flows can be tested. Device builds are not set up.

## Prerequisites (macOS, Apple Silicon, Xcode 26+)

```bash
pip install aqtinstall
# from the repo root
aqt install-qt mac ios     6.8.3 ios      -m qtcharts qtwebsockets qtmultimedia qtspeech -O .qt
aqt install-qt mac desktop 6.8.3 clang_64 -m qtcharts qtwebsockets qtmultimedia qtspeech -O .qt
# OpenSSL 3.x for the simulator (static)
./Configure iossimulator-arm64-xcrun no-shared no-tests no-apps no-docs --prefix=$HOME/ios-deps/openssl-sim
make -j8 && make install_sw
```

## Build and run

```bash
cd fincept-qt
OPENSSL_IOS_SIM=$HOME/ios-deps/openssl-sim ./packaging/ios/build_simulator.sh
xcrun simctl boot "iPhone 17"; open -a Simulator
xcrun simctl install booted build/ios-simulator/FinceptTerminal.app
xcrun simctl launch booted com.fincept.terminal
```

Qt 6.8.3's prebuilt iOS kit ships only an x86_64 simulator slice, so the script
clones `.qt/6.8.3/ios` to `.qt/6.8.3/ios-sim` once and relabels its arm64 device
objects as simulator objects (`packaging/ios/retarget_qt_arm64_simulator.py`).

## What is disabled on iOS

- `QProcess` does not exist in Qt for iOS. `src/platform/ios/IosProcessStub.h` stands
  in and always fails to start, so everything that shells out is unavailable: the
  embedded Python runtime and all Python-backed analytics/data, MCP servers, agents,
  coding agents, TTS/STT helpers, the updater and yt-dlp.
- Python setup is skipped at startup and its banner is hidden.
- Qt's FFmpeg media plugin is excluded (AVFoundation is used); QtWebEngine is not available.
- Desktop resource files (`resources/`) and the `.icns` icon are not bundled.

## Phone layout adjustments

The desktop UI is kept; only what blocked the tested flows was changed: no 800px
window / 480px auth minimums, toolbars may clip instead of widening the window,
the tab bar swipes, the News detail panel replaces the feed, and Notes stacks the
list above the editor (category sidebar hidden).
