// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.linkspan.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

/** An Android program started by Linkspan on this phone. */
class GuestDemoActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            LinkspanTheme(dark = isSystemInDarkTheme()) {
                Column(
                    modifier = Modifier.fillMaxSize().padding(24.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Text("Android example", style = MaterialTheme.typography.headlineMedium)
                    Text("Linkspan started this Android screen on this device. It is an Android program running on Android, not a program from another system.")
                }
            }
        }
    }
}
