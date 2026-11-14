// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.linkspan.android

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.widget.Toast

class InstallResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
        val text = when (status) {
            PackageInstaller.STATUS_SUCCESS -> "Android installed the APK."
            PackageInstaller.STATUS_PENDING_USER_ACTION -> "Confirm the install in the Android dialog."
            else -> message ?: "Android did not install the APK."
        }
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
            confirm?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            if (confirm != null) {
                context.startActivity(confirm)
                return
            }
        }
        Toast.makeText(context, text, Toast.LENGTH_LONG).show()
    }
}
