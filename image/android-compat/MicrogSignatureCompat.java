/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-FileCopyrightText: 2026 Daniel Ye
 * SPDX-License-Identifier: Apache-2.0
 * Adapted limited microG policy; this is an experimental framework client-query hook.
 */
package org.olympium.android.compat;

import android.content.pm.PackageInfo;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import java.io.ByteArrayInputStream;
import java.lang.reflect.Method;
import java.security.PublicKey;
import java.security.cert.CertificateFactory;
import java.util.Collections;

public final class MicrogSignatureCompat {
    private static volatile PublicKey googleKey;
    private static volatile boolean warned;

    private MicrogSignatureCompat() { }

    private static Object call(Object target, String name) throws ReflectiveOperationException {
        Method method = target.getClass().getMethod(name);
        method.setAccessible(true);
        return method.invoke(target);
    }

    private static synchronized PublicKey googleKey() throws Exception {
        if (googleKey == null) {
            googleKey = CertificateFactory.getInstance("X.509").generateCertificate(
                    new ByteArrayInputStream(TrustedCertificates.FAKE_SIGNATURE.toByteArray())).getPublicKey();
        }
        return googleKey;
    }

    /**
     * Only rewrites the newly generated public PackageInfo. The parsed APK's
     * SigningDetails, package installer, signature permissions and update
     * verification remain untouched.
     */
    public static PackageInfo rewrite(PackageInfo info, Object packageState) {
        if (info == null || packageState == null || Build.VERSION.SDK_INT != 37 ||
                !("userdebug".equals(Build.TYPE) || "eng".equals(Build.TYPE))) return info;
        if (!("com.google.android.gms".equals(info.packageName) ||
                "com.android.vending".equals(info.packageName))) return info;
        try {
            Object parsed = call(packageState, "getPkg");
            if (parsed == null || !info.packageName.equals(call(parsed, "getPackageName"))) return info;
            Object signing = call(parsed, "getSigningDetails");
            if (signing == null) return info;
            Signature[] real = (Signature[]) call(signing, "getSignatures");
            if (real == null || real.length != 1 ||
                    !TrustedCertificates.REAL_SIGNATURE.equals(real[0])) return info;
            Bundle metadata = (Bundle) call(parsed, "getMetaData");
            if (metadata == null) return info;
            String requested = metadata.getString("fake-signature");
            if (requested == null || !TrustedCertificates.FAKE_SIGNATURE.equals(new Signature(requested))) return info;
            // Construct everything before updating the client-facing object.
            // A failure keeps the original signatures instead of broadening trust.
            SigningInfo replacement = new SigningInfo(SigningInfo.VERSION_SIGNING_BLOCK_V3,
                    Collections.singleton(TrustedCertificates.FAKE_SIGNATURE),
                    Collections.singleton(googleKey()), null);
            info.signatures = new Signature[]{TrustedCertificates.FAKE_SIGNATURE};
            info.signingInfo = replacement;
        } catch (Exception error) {
            if (!warned) {
                warned = true;
                Log.e("OlympiumMicrogCompat", "Limited signature compatibility unavailable", error);
            }
        }
        return info;
    }
}
