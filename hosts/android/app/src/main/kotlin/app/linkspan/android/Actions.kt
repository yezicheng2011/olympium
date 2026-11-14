// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.linkspan.android

import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import app.linkspan.protocol.LaunchGate
import java.io.File

object Actions {
    fun launchPackage(context: Context, packageName: String): String {
        val intent = context.packageManager.getLaunchIntentForPackage(packageName)
            ?: return "Android has no launcher activity for $packageName."
        context.startActivity(intent)
        return "Opened $packageName."
    }

    fun installApk(context: Context, apk: File): String {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        val sessionId = installer.createSession(params)
        installer.openSession(sessionId).use { session ->
            apk.inputStream().use { input ->
                session.openWrite("base.apk", 0, apk.length()).use { output ->
                    input.copyTo(output)
                    session.fsync(output)
                }
            }
            val intent = Intent(context, InstallResultReceiver::class.java)
            val flags = if (Build.VERSION.SDK_INT >= 31) {
                android.app.PendingIntent.FLAG_UPDATE_CURRENT or android.app.PendingIntent.FLAG_MUTABLE
            } else {
                android.app.PendingIntent.FLAG_UPDATE_CURRENT
            }
            val pending = android.app.PendingIntent.getBroadcast(context, sessionId, intent, flags)
            session.commit(pending.intentSender)
        }
        return "Asked Android to install ${apk.name}. Confirm it if Android asks. Linkspan does not include Google Play."
    }

    fun runHook(context: Context, guest: String, level: String, command: String, file: File): String {
        if (LaunchGate.mustRefuse("android", guest, level)) {
            return LaunchGate.refusalMessage("android", guest)
        }
        if (command.isBlank()) {
            return "This needs a runtime command in Settings. Linkspan does not include a Linux or Windows runtime."
        }
        return try {
            val process = ProcessBuilder(command, file.absolutePath)
                .redirectErrorStream(true)
                .start()
            val output = process.inputStream.bufferedReader().readText().take(4000)
            val code = process.waitFor()
            if (output.isBlank()) "Exit code $code." else output
        } catch (error: Exception) {
            "Could not start the runtime command. Android usually blocks a normal app from launching another system. ${error.message}"
        }
    }
}
