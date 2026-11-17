# Android (x86_64 emulator) build

Experimental. Builds the Qt Widgets app as an APK for an x86_64 Android emulator, from a Linux host.

## What is different on Android

- **No embedded Python.** uv/Python cannot be provisioned inside the app sandbox, so first-run Python setup and package sync are skipped (`main.cpp`, `Q_OS_ANDROID`). Python-backed widgets and features (analytics, agents, MCP, TTS/STT, other `QProcess` users) are unavailable and the app shows a "PYTHON NOT SET UP" banner.
- **QtWebEngine** is not available (already optional in CMake).
- **QtADS** is patched at fetch time (`cmake/qtads_android_patch.cmake`) to take its non-X11 code paths.
- **OpenSSL** comes from prebuilt [KDAB/android_openssl](https://github.com/KDAB/android_openssl) libraries, bundled into the APK.
- **Repaint workaround.** Qt 6.8 on Android only flushes the dirty sub-rect of a widget window, which leaves stale pixels after page switches; `main.cpp` coalesces widget repaints into a full-window repaint.

## Prerequisites (Linux x86_64)

```sh
# JDK 17, Android SDK + NDK + emulator image
sudo apt-get install -y openjdk-17-jdk-headless ninja-build
mkdir -p ~/android-sdk/cmdline-tools && cd ~/android-sdk/cmdline-tools
curl -LO https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip -q commandlinetools-linux-*.zip && mv cmdline-tools latest
yes | latest/bin/sdkmanager --licenses
latest/bin/sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" \
  "ndk;26.1.10909125" "emulator" "system-images;android-34;google_apis;x86_64"

# Qt 6.8.3 for Android x86_64 + matching desktop host kit (run from the repo root)
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O .qt -m qtcharts qtwebsockets qtmultimedia qtspeech
aqt install-qt linux android 6.8.3 android_x86_64 -O .qt -m qtcharts qtwebsockets qtmultimedia qtspeech

# OpenSSL 3 for Android
git clone --depth 1 https://github.com/KDAB/android_openssl ~/android_openssl
```

Environment (paths as above, run from the repo root):

```sh
export QT_ANDROID=$PWD/.qt/6.8.3/android_x86_64
export QT_HOST_PATH=$PWD/.qt/6.8.3/gcc_64
export ANDROID_SDK_ROOT=$HOME/android-sdk ANDROID_HOME=$HOME/android-sdk
export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/26.1.10909125
export FINCEPT_ANDROID_OPENSSL_ROOT=$HOME/android_openssl/no-asm/ssl_3
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export PATH=$ANDROID_SDK_ROOT/platform-tools:$PATH
```

## Build

```sh
cd fincept-qt
cmake --preset android-x86_64
cmake --build --preset android-x86_64      # target FinceptTerminal_make_apk
```

The unsigned APK is at `build/android-x86_64/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`. Sign it with the debug key:

```sh
BT=$ANDROID_SDK_ROOT/build-tools/34.0.0
keytool -genkeypair -keystore ~/.android/debug.keystore -storepass android -keypass android \
  -alias androiddebugkey -keyalg RSA -validity 10000 -dname "CN=Android Debug" 2>/dev/null || true
$BT/zipalign -f 4 build/android-x86_64/android-build/build/outputs/apk/release/android-build-release-unsigned.apk /tmp/aligned.apk
$BT/apksigner sign --ks ~/.android/debug.keystore --ks-pass pass:android --out FinceptTerminal-x86_64.apk /tmp/aligned.apk
```

If Gradle gets HTTP 429 from Maven Central, point it at a mirror with a `~/.gradle/init.d/*.gradle` init script.

## Run on an emulator

```sh
echo no | $ANDROID_SDK_ROOT/cmdline-tools/latest/bin/avdmanager create avd -n fincept \
  -k "system-images;android-34;google_apis;x86_64" -d pixel_tablet
$ANDROID_SDK_ROOT/emulator/emulator -avd fincept -gpu swiftshader_indirect &
adb wait-for-device && adb install -r FinceptTerminal-x86_64.apk
adb shell monkey -p com.fincept.terminal 1
```

A tablet-sized landscape AVD is recommended; the desktop layout is not adapted to phones.

## Known issues

- Touch input does not reach buttons in modal `QMessageBox` dialogs (e.g. the Notes delete confirmation); keyboard (Tab + Enter) works.
- Touch swipes do not scroll Qt Widgets scroll areas; drag the scrollbar instead.
