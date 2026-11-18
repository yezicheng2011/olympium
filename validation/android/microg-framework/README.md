# Limited microG framework experiment

An independent ordinary-UID Android 17 client initially reported microG's real
certificate and failed compatibility checks. With this source-built hook it
reported the expected legacy/modern GMS and Companion certificates and completed
a real Common service broker request/callback. The identical client APK was used
before and after the change. Records are in `../evidence/api37-microg-*`.

The update boundary was also exercised: reinstalling the original signed
Companion succeeded; a valid APK signed with the disposable probe key was rejected
with INSTALL_FAILED_UPDATE_INCOMPATIBLE; the installed Companion SHA256 stayed
unchanged. This is not verification of Maps, FCM, accounts, attestation or all GMS
APIs. The full-GMS result deliberately remains false.

The policy follows LineageOS's limited microG support, pinned in `../../../image/android-compat/profile.json`:
only com.google.android.gms and com.android.vending, the real microG certificate,
the exact expected fake-signature manifest metadata, and a debuggable Android 17
build qualify. Other package identities return unchanged. It rewrites the new
client-facing PackageInfo object. Parsed package SigningDetails, package updates
and signature permissions retain the original platform implementation.

`build.py` accepts only the pinned official SDK's services.jar digest. It wraps
the existing ComputerEngine.generatePackageInfo implementation without editing its
register allocation, branches or exception handling, then appends the helper DEX.
It requires a JDK, Ubuntu's libsmali-java tools and the pinned validation SDK:

```sh
python3 validation/android/microg-framework/build.py \
  --services-jar /path/to/verified-original-services.jar \
  --sdk build/runtime-validation/gms-probe-sdk \
  --output build/runtime-validation/microg-framework-build
```

Use a fresh output directory. The builder does not deploy anything. Source and
output hashes are saved in build-result.json. Never commit generated frameworks,
SDK binaries, APKs or disposable signing keys.

The passing experiment used a rooted disposable AVD, a read-only bind of the
result over services.jar in init's mount namespace, and a userspace zygote restart.
SELinux stayed Enforcing and the verity setting stayed enforcing. No adb remount,
disable-verity or package-signature-verification bypass was used. `../../../image/android-compat/remount-ro.c`
is an already-root-only validation helper for the fixed framework path; it also
supports restoring the original mount. It is not a setuid tool.

The shared policy sources now live in `image/android-compat/`. The managed-runtime
controller and build-time support assets are under active first-boot and cold-start
validation; this evidence alone does not certify their automatic deployment or a
new ISO. These remain separate acceptance requirements. Never apply the experiment to a physical phone
or another person's environment. The wrong-signer fixture intentionally uses the
Companion package name and belongs only in a disposable AVD.

The certificate constants and limited-policy adaptation retain LineageOS's
Apache-2.0 notices; see `../../../image/android-compat/LICENSE` and the immutable upstream link in its profile.json.
