// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.linkspan.android

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val store = LibraryStore(this)
        setContent {
            val prefs = getSharedPreferences(LibraryStore.PREFS, MODE_PRIVATE)
            var appearance by remember { mutableStateOf(prefs.getString("appearance", "system") ?: "system") }
            val dark = when (appearance) {
                "light" -> false
                "dark" -> true
                else -> isSystemInDarkTheme()
            }
            LinkspanTheme(dark = dark) {
                LinkspanApp(
                    store = store,
                    appearance = appearance,
                    onAppearance = { value ->
                        appearance = value
                        prefs.edit().putString("appearance", value).apply()
                    },
                    onOpenDemo = {
                        startActivity(Intent(this, GuestDemoActivity::class.java))
                    },
                )
            }
        }
    }
}
