# Independent Android 17 / microG checks

This small, debuggable validation APK uses public Android APIs and a Binder
service request. It does not include Google SDK code or assets, and is not
staged in the production ISO.

It runs as an ordinary application UID and checks:

* The legacy and modern PackageManager signing-certificate results for GMS.
* The Companion package's legacy signing-certificate result.
* A public GMS Common service broker connection, actual getService request,
  success callback and returned ICommonService Binder descriptor.

A successful service handshake is narrower than complete GMS compatibility.
Maps, FCM delivery, accounts, licensing and attestation are not tested.
`gms_api_compatibility_verified` deliberately remains false. Declaring a
FAKE_PACKAGE_SIGNATURE permission is insufficient: the signature results
returned to an actual client must match too.

The independent wire implementation follows the public interfaces in microG
[v0.3.18.252432](https://github.com/microg/GmsCore/tree/v0.3.18.252432):
GetServiceRequest fields, IGmsCallbacks, the IGmsServiceBroker getService
transaction, and GmsService.COMMON (39). It never clears accounts or changes
other applications' settings. The verifier clears only its own probe data
before each run and checks installed APK bytes to exclude stale/substitute
results.

On Linux x86_64 with Python 3, a JDK and the prepared Android runtime:

```sh
python3 validation/android/gms-probe/prepare-sdk.py build/runtime-validation/gms-probe-sdk
sh validation/android/gms-probe/build.sh \
  build/runtime-validation/gms-probe-sdk build/runtime-validation/gms-probe-build
```

Use a fresh destination for `prepare-sdk.py`. The SDK platform and build tools
are pinned against Google's repository metadata in `build-tools.json`.
Generated APKs, SDK downloads, build output and the disposable debug keystore
belong only in ignored `build/runtime-validation/`. Never commit the keystore.

Install the pinned microG Services and Companion in the selected AOSP Android
17 runtime first, then run the verifier with the APK SHA256 printed by the
build. `--container` is optional; the APK's absolute path must be visible in
both environments when it is used.

```sh
python3 validation/android/gms-probe/verify.py \
  --container bbb-validation --adb /absolute/platform-tools/adb \
  --serial emulator-5558 \
  --apk /absolute/build/runtime-validation/gms-probe-build/gms-probe.apk \
  --apk-sha256 ACTUAL_BUILD_SHA256 \
  --output build/runtime-validation/gms-probe-result
```

A nonzero exit or `failed-checks` is a failed validation, even when the APK
installed or the app produced JSON. Building and signature-verifying this
probe APK does not establish that it executed. Check recorded runtime
results separately.
