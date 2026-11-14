// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

package app.olympium.android

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

data class StoredProgram(
    val id: String,
    val name: String,
    val guest: String,
    val kind: String,
    val target: String,
)

class LibraryStore(context: Context) {
    private val file = File(context.filesDir, "library.json")

    fun list(): List<StoredProgram> {
        if (!file.exists()) {
            return emptyList()
        }
        val array = JSONArray(file.readText())
        return (0 until array.length()).map { index ->
            val item = array.getJSONObject(index)
            StoredProgram(
                id = item.getString("id"),
                name = item.getString("name"),
                guest = item.getString("guest"),
                kind = item.getString("kind"),
                target = item.getString("target"),
            )
        }
    }

    fun add(name: String, guest: String, kind: String, target: String) {
        val programs = list().toMutableList()
        programs.add(
            StoredProgram(
                id = UUID.randomUUID().toString(),
                name = name,
                guest = guest,
                kind = kind,
                target = target,
            ),
        )
        write(programs)
    }

    fun remove(id: String) {
        write(list().filter { it.id != id })
    }

    private fun write(programs: List<StoredProgram>) {
        val array = JSONArray()
        for (program in programs) {
            array.put(
                JSONObject()
                    .put("id", program.id)
                    .put("name", program.name)
                    .put("guest", program.guest)
                    .put("kind", program.kind)
                    .put("target", program.target),
            )
        }
        file.writeText(array.toString())
    }

    companion object {
        const val PREFS = "olympium"
    }
}

fun guessProgram(fileName: String): Pair<String, String> {
    val lower = fileName.lowercase()
    return when {
        lower.endsWith(".apk") -> "android" to "android_apk"
        lower.endsWith(".exe") || lower.endsWith(".msi") || lower.endsWith(".bat") -> "windows" to "windows_exe"
        lower.endsWith(".dmg") || lower.endsWith(".pkg") || lower.endsWith(".app") -> "macos" to "macos_dmg"
        lower.endsWith(".ipa") -> "ios" to "ios_package"
        lower.endsWith(".deb") || lower.endsWith(".rpm") || lower.endsWith(".appimage") -> "linux" to "linux_deb"
        else -> "linux" to "linux_executable"
    }
}
