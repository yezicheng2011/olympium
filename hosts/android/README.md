# Linkspan for Android

This is the Android client. There is no iOS client.

It installs an APK you already have, using Android's own installer, and it can open an app that is already installed. The Android example screen is started from Overview so you can see that path on a device.

Linux and Windows programs are hooks. Put the path of a runtime you installed into Settings. Linkspan does not include that runtime. macOS and iOS files can be added and will not be started. The refusal rule is in `protocol/` and has a small Java test that does not need the Android SDK:

```bash
javac -d /tmp/linkspan-java \
  hosts/android/protocol/src/main/java/app/linkspan/protocol/LaunchGate.java \
  hosts/android/protocol/src/test/java/app/linkspan/protocol/LaunchGateTest.java
java -cp /tmp/linkspan-java app.linkspan.protocol.LaunchGateTest
```

## Build the app

Install Android SDK 35 and a JDK, then from `hosts/android`:

```bash
./gradlew :app:assembleDebug
```

The debug APK is the build-from-source output. This repository does not publish it.
