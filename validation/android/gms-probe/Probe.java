/* Independent Android platform/Binder probe. No Google SDK or service assets. */
package org.olympium.validation.gms;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.widget.TextView;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class Probe extends Activity {
    private static final String GMS = "com.google.android.gms";
    private static final String GOOGLE_SHA1 = "38918a453d07199354f8b19af05ec6562ced5788";
    private static final String BROKER = "com.google.android.gms.common.internal.IGmsServiceBroker";
    private static final String CALLBACKS = "com.google.android.gms.common.internal.IGmsCallbacks";
    private static final String COMMON = "com.google.android.gms.common.internal.service.ICommonService";
    private final CountDownLatch response = new CountDownLatch(1);
    private volatile String brokerDescriptor = "", serviceDescriptor = "", error = "";
    private volatile int serviceStatus = Integer.MIN_VALUE;
    private TextView text;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        text = new TextView(this);
        text.setPadding(24, 24, 24, 24);
        text.setText("Checking installed GMS signatures and service handshake…");
        setContentView(text);
        new Thread(this::check, "gms-probe").start();
    }

    private static String sha1(Signature signature) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(signature.toByteArray());
        StringBuilder out = new StringBuilder();
        for (byte value : digest) out.append(String.format("%02x", value & 255));
        return out.toString();
    }

    private static String signatureDigest(Signature[] signatures) throws Exception {
        return signatures != null && signatures.length == 1 ? sha1(signatures[0]) : "";
    }

    private void check() {
        JSONObject result = new JSONObject();
        boolean bound = false;
        ServiceConnection connection = new ServiceConnection() {
            @Override public void onServiceConnected(ComponentName name, IBinder binder) {
                new Thread(() -> requestService(binder), "gms-request").start();
            }
            @Override public void onServiceDisconnected(ComponentName name) {
                error = "Broker disconnected";
                response.countDown();
            }
            @Override public void onNullBinding(ComponentName name) {
                error = "Broker returned null binding";
                response.countDown();
            }
            @Override public void onBindingDied(ComponentName name) {
                error = "Broker binding died";
                response.countDown();
            }
        };
        try {
            result.put("android_release", Build.VERSION.RELEASE);
            result.put("android_api", Build.VERSION.SDK_INT);
            result.put("caller_package", getPackageName());
            result.put("caller_uid", android.os.Process.myUid());
            PackageManager pm = getPackageManager();
            PackageInfo legacy = pm.getPackageInfo(GMS, PackageManager.GET_SIGNATURES);
            PackageInfo modern = pm.getPackageInfo(GMS, PackageManager.GET_SIGNING_CERTIFICATES);
            String legacySha = signatureDigest(legacy.signatures);
            String modernSha = modern.signingInfo == null ? "" :
                    signatureDigest(modern.signingInfo.getApkContentsSigners());
            result.put("gms_version", legacy.versionName);
            result.put("legacy_signature_sha1", legacySha);
            result.put("modern_signature_sha1", modernSha);
            result.put("signature_compatibility_verified",
                    GOOGLE_SHA1.equals(legacySha) && GOOGLE_SHA1.equals(modernSha));
            try {
                PackageInfo store = pm.getPackageInfo("com.android.vending", PackageManager.GET_SIGNATURES);
                String storeSha = signatureDigest(store.signatures);
                result.put("companion_signature_sha1", storeSha);
                result.put("companion_signature_compatibility_verified", GOOGLE_SHA1.equals(storeSha));
            } catch (PackageManager.NameNotFoundException e) {
                result.put("companion_signature_compatibility_verified", false);
                result.put("companion_error", e.toString());
            }
            bound = bindService(new Intent("com.google.android.gms.common.service.START")
                    .setPackage(GMS), connection, BIND_AUTO_CREATE);
            result.put("broker_binding_accepted", bound);
            if (bound && !response.await(60, TimeUnit.SECONDS)) error = "Service callback timed out";
            result.put("broker_descriptor", brokerDescriptor);
            result.put("service_descriptor", serviceDescriptor);
            result.put("service_status", serviceStatus);
            result.put("gms_service_handshake_verified", bound && BROKER.equals(brokerDescriptor)
                    && serviceStatus == 0 && COMMON.equals(serviceDescriptor) && error.isEmpty());
            result.put("error", error);
            // One public service handshake does not certify Maps, FCM,
            // accounts, licensing, attestation or arbitrary GMS clients.
            result.put("gms_api_compatibility_verified", false);
            result.put("probe_completed", true);
        } catch (Exception e) {
            try {
                result.put("probe_completed", false);
                result.put("gms_service_handshake_verified", false);
                result.put("gms_api_compatibility_verified", false);
                result.put("error", e.toString());
            } catch (Exception ignored) { }
        } finally {
            if (bound) unbindService(connection);
        }
        try {
            String report = result.toString(2);
            File temporary = new File(getFilesDir(), "result.tmp");
            try (FileOutputStream out = new FileOutputStream(temporary)) {
                out.write(report.getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (!temporary.renameTo(new File(getFilesDir(), "result.json")))
                throw new IllegalStateException("Could not save report");
            runOnUiThread(() -> text.setText(report));
        } catch (Exception e) {
            runOnUiThread(() -> text.setText("Report failed: " + e));
        }
    }

    private void requestService(IBinder broker) {
        Parcel request = Parcel.obtain(), reply = Parcel.obtain();
        try {
            brokerDescriptor = broker.getInterfaceDescriptor();
            if (!BROKER.equals(brokerDescriptor)) throw new IllegalStateException("Unexpected broker");
            Binder callback = new Binder() {
                @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (code == INTERFACE_TRANSACTION) {
                        if (reply != null) reply.writeString(CALLBACKS);
                        return true;
                    }
                    if (code != 1 && code != 3) return super.onTransact(code, data, reply, flags);
                    data.enforceInterface(CALLBACKS);
                    serviceStatus = data.readInt();
                    IBinder service = data.readStrongBinder();
                    serviceDescriptor = service == null ? "" : service.getInterfaceDescriptor();
                    if (reply != null) reply.writeNoException();
                    response.countDown();
                    return true;
                }
            };
            request.writeInterfaceToken(BROKER);
            request.writeStrongBinder(callback);
            request.writeInt(1); // Present GetServiceRequest parcelable.
            request.writeInt(0xffff4f45); // SafeParcelable object header.
            int lengthOffset = request.dataPosition();
            request.writeInt(0);
            int start = request.dataPosition();
            intField(request, 1, 6); // Parcel version.
            intField(request, 2, 39); // COMMON, a non-account service.
            intField(request, 3, 12451000); // Client library protocol version.
            int field = variableField(request, 4);
            request.writeString(getPackageName());
            endVariable(request, field);
            intField(request, 6, 0); // Empty Scope[].
            field = variableField(request, 7);
            request.writeBundle(new Bundle());
            endVariable(request, field);
            intField(request, 10, 0); // Empty Feature[].
            intField(request, 11, 0);
            intField(request, 12, 0); // Legacy onPostInitComplete callback.
            int end = request.dataPosition();
            request.setDataPosition(lengthOffset);
            request.writeInt(end - start);
            request.setDataPosition(end);
            if (!broker.transact(46, request, reply, 0))
                throw new IllegalStateException("getService transaction rejected");
            reply.readException();
        } catch (Exception e) {
            error = e.toString();
            response.countDown();
        } finally {
            request.recycle();
            reply.recycle();
        }
    }

    private static void intField(Parcel parcel, int id, int value) {
        parcel.writeInt((4 << 16) | id);
        parcel.writeInt(value);
    }
    private static int variableField(Parcel parcel, int id) {
        parcel.writeInt(0xffff0000 | id);
        int offset = parcel.dataPosition();
        parcel.writeInt(0);
        return offset;
    }
    private static void endVariable(Parcel parcel, int offset) {
        int end = parcel.dataPosition();
        parcel.setDataPosition(offset);
        parcel.writeInt(end - offset - 4);
        parcel.setDataPosition(end);
    }
}
