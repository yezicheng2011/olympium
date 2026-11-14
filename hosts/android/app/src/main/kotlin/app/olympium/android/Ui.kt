// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.olympium.android

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import app.olympium.protocol.LaunchGate
import org.json.JSONObject
import java.io.File

@Composable
fun OlympiumTheme(dark: Boolean, content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = if (dark) darkColorScheme() else lightColorScheme(),
        content = content,
    )
}

private enum class Page(val label: String) {
    Overview("Overview"),
    Programs("Programs"),
    Compatibility("Compatibility"),
    Limits("Limits"),
    Settings("Settings"),
}

@Composable
fun OlympiumApp(
    store: LibraryStore,
    appearance: String,
    onAppearance: (String) -> Unit,
    onOpenDemo: () -> Unit,
) {
    var page by remember { mutableIntStateOf(0) }
    val pages = Page.entries
    Scaffold(
        bottomBar = {
            NavigationBar {
                pages.forEachIndexed { index, item ->
                    NavigationBarItem(
                        selected = page == index,
                        onClick = { page = index },
                        icon = { Text(item.label.take(1)) },
                        label = { Text(item.label) },
                    )
                }
            }
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            when (pages[page]) {
                Page.Overview -> OverviewPage(onOpenDemo)
                Page.Programs -> ProgramsPage(store)
                Page.Compatibility -> JsonPage(arrayName = "cells")
                Page.Limits -> JsonPage(arrayName = "limits")
                Page.Settings -> SettingsPage(appearance, onAppearance)
            }
        }
    }
}

@Composable
private fun OverviewPage(onOpenDemo: () -> Unit) {
    Text("Programs from other systems, on this Android device.", style = MaterialTheme.typography.headlineSmall)
    Text("Android apps can be installed from an APK you already have, or opened if they are already installed. Linux and Windows programs need a runtime command you configure. macOS and iOS software will not run here.")
    Button(onClick = onOpenDemo) { Text("Open the Android example") }
}

@Composable
private fun ProgramsPage(store: LibraryStore) {
    val context = LocalContext.current
    var programs by remember { mutableStateOf(store.list()) }
    var packageName by remember { mutableStateOf("") }
    var status by remember { mutableStateOf("") }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            val name = queryName(context, uri)
            val (guest, kind) = guessProgram(name)
            val copy = copyToInbox(context, uri, name)
            store.add(name, guest, kind, copy.absolutePath)
            programs = store.list()
            status = "Added $name."
        }
    }
    Text("Programs", style = MaterialTheme.typography.headlineSmall)
    Button(onClick = { picker.launch(arrayOf("*/*")) }) { Text("Add a file") }
    OutlinedTextField(
        value = packageName,
        onValueChange = { packageName = it },
        label = { Text("Installed Android package") },
        modifier = Modifier.fillMaxWidth(),
    )
    Button(onClick = { status = Actions.launchPackage(context, packageName.trim()) }) {
        Text("Open package")
    }
    if (programs.isEmpty()) {
        Text("No programs yet.")
    }
    programs.forEach { program ->
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text("${program.name} — ${program.guest}")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = {
                    status = runProgram(context, program)
                }) { Text("Run") }
                Button(onClick = {
                    store.remove(program.id)
                    programs = store.list()
                }) { Text("Remove") }
            }
        }
    }
    if (status.isNotEmpty()) {
        Text(status)
    }
}

@Composable
private fun JsonPage(arrayName: String) {
    val context = LocalContext.current
    val root = remember {
        JSONObject(context.assets.open("compatibility.json").bufferedReader().readText())
    }
    Text(if (arrayName == "limits") "Limits" else "Compatibility", style = MaterialTheme.typography.headlineSmall)
    val array = root.getJSONArray(arrayName)
    for (index in 0 until array.length()) {
        val item = array.getJSONObject(index)
        if (arrayName == "cells") {
            Text("${item.getString("host")} / ${item.getString("guest")} — ${item.getString("title")}")
            Text(item.getString("summary"), style = MaterialTheme.typography.bodyMedium)
        } else {
            Text(item.getString("title"), style = MaterialTheme.typography.titleMedium)
            Text(item.getString("body"))
        }
    }
}

@Composable
private fun SettingsPage(appearance: String, onAppearance: (String) -> Unit) {
    val context = LocalContext.current
    val prefs = context.getSharedPreferences(LibraryStore.PREFS, Context.MODE_PRIVATE)
    var linuxHook by remember { mutableStateOf(prefs.getString("linux_hook", "") ?: "") }
    var windowsHook by remember { mutableStateOf(prefs.getString("windows_hook", "") ?: "") }
    Text("Settings", style = MaterialTheme.typography.headlineSmall)
    Text("Appearance follows the system unless you choose otherwise.")
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        listOf("system" to "System", "light" to "Light", "dark" to "Dark").forEach { (value, label) ->
            Button(onClick = { onAppearance(value) }, enabled = appearance != value) { Text(label) }
        }
    }
    OutlinedTextField(
        value = linuxHook,
        onValueChange = { linuxHook = it },
        label = { Text("Linux runtime command") },
        modifier = Modifier.fillMaxWidth(),
    )
    OutlinedTextField(
        value = windowsHook,
        onValueChange = { windowsHook = it },
        label = { Text("Windows runtime command") },
        modifier = Modifier.fillMaxWidth(),
    )
    Button(onClick = {
        prefs.edit().putString("linux_hook", linuxHook.trim()).putString("windows_hook", windowsHook.trim()).apply()
    }) { Text("Save commands") }
    Text("Leave these empty until you have a runtime of your own. This Android app does not download a guest image and it will not translate an OpenHarmony HAP. macOS and iOS software will not run.")
}

private fun runProgram(context: Context, program: StoredProgram): String {
    val level = cellLevel(context, program.guest)
    if (LaunchGate.mustRefuse("android", program.guest, level)) {
        return LaunchGate.refusalMessage("android", program.guest)
    }
    val file = File(program.target)
    return when (program.guest) {
        "android" -> if (program.kind == "android_apk") {
            Actions.installApk(context, file)
        } else {
            Actions.launchPackage(context, program.target)
        }
        "linux" -> Actions.runHook(context, program.guest, level, hook(context, "linux_hook"), file)
        "windows" -> Actions.runHook(context, program.guest, level, hook(context, "windows_hook"), file)
        else -> LaunchGate.refusalMessage("android", program.guest)
    }
}

private fun cellLevel(context: Context, guest: String): String {
    val root = JSONObject(context.assets.open("compatibility.json").bufferedReader().readText())
    val cells = root.getJSONArray("cells")
    for (index in 0 until cells.length()) {
        val cell = cells.getJSONObject(index)
        if (cell.getString("host") == "android" && cell.getString("guest") == guest) {
            return cell.getString("level")
        }
    }
    return "refused"
}

private fun hook(context: Context, key: String): String {
    return context.getSharedPreferences(LibraryStore.PREFS, Context.MODE_PRIVATE).getString(key, "") ?: ""
}

private fun queryName(context: Context, uri: Uri): String {
    val cursor = context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)
    cursor?.use {
        if (it.moveToFirst()) {
            val index = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (index >= 0) {
                return it.getString(index)
            }
        }
    }
    return "program"
}

private fun copyToInbox(context: Context, uri: Uri, name: String): File {
    val dir = File(context.filesDir, "inbox")
    dir.mkdirs()
    val dest = File(dir, name.replace(Regex("[^A-Za-z0-9._-]"), "_"))
    context.contentResolver.openInputStream(uri).use { input ->
        dest.outputStream().use { output ->
            input?.copyTo(output)
        }
    }
    return dest
}
