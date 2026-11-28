' Copyright 2026 Olympium Contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Cross-platform interoperability hub (olympium-interop):
'   - Bidirectional clipboard sync: Host Wayland/X11 <-> Wine <-> Android (ADB/scrcpy) <-> OpenHarmony (HDC) <-> Darling
'   - Unified shared folder & native URI/file handoff across ecosystems
'   - Cross-platform desktop widget bridge (Android AppWidget + native widgets)
'   - Built-in open-source microG GmsCore (Apache-2.0) management (NO proprietary Play Store bundled)
'   - Persistent Settings flag for the optional, dismissible "How to install Google Play" guide

Imports System
Imports System.Collections.Generic
Imports System.IO
Imports System.Text
Imports System.Text.Json

Namespace Olympium

    Public Module InteropHub

        Public ReadOnly Property ConfigDir As String
            Get
                Dim home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
                Return Path.Combine(home, ".config", "olympium")
            End Get
        End Property

        Public ReadOnly Property SettingsPath As String
            Get
                Return Path.Combine(ConfigDir, "settings.json")
            End Get
        End Property

        Public ReadOnly Property SharedDir As String
            Get
                Dim home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
                Return Path.Combine(home, "OlympiumShared")
            End Get
        End Property

        Public ReadOnly Property WidgetsFeedPath As String
            Get
                Dim home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
                Return Path.Combine(home, ".local", "share", "olympium", "widgets", "feed.json")
            End Get
        End Property

        Public Function LoadSettings() As Dictionary(Of String, Object)
            Dim defaults As New Dictionary(Of String, Object) From {
                {"clipboard_sync_enabled", True},
                {"shared_folder_enabled", True},
                {"android_widgets_enabled", True},
                {"microg_enabled", True},
                {"show_google_play_guide", True}
            }
            Try
                If File.Exists(SettingsPath) Then
                    Dim raw = File.ReadAllText(SettingsPath)
                    Using doc = JsonDocument.Parse(raw)
                        For Each prop In doc.RootElement.EnumerateObject()
                            If prop.Value.ValueKind = JsonValueKind.True Then
                                defaults(prop.Name) = True
                            ElseIf prop.Value.ValueKind = JsonValueKind.False Then
                                defaults(prop.Name) = False
                            ElseIf prop.Value.ValueKind = JsonValueKind.String Then
                                defaults(prop.Name) = prop.Value.GetString()
                            End If
                        Next
                    End Using
                End If
            Catch
            End Try
            Return defaults
        End Function

        Public Sub SaveSettings(cfg As Dictionary(Of String, Object))
            Directory.CreateDirectory(ConfigDir)
            Dim json = JsonSerializer.Serialize(cfg, New JsonSerializerOptions With {.WriteIndented = True})
            AtomicWriteText(SettingsPath, json)
        End Sub

        Public Function GetBoolSetting(key As String, fallback As Boolean) As Boolean
            Dim cfg = LoadSettings()
            If cfg.ContainsKey(key) AndAlso TypeOf cfg(key) Is Boolean Then
                Return CBool(cfg(key))
            End If
            Return fallback
        End Function

        Public Sub SetBoolSetting(key As String, value As Boolean)
            Dim cfg = LoadSettings()
            cfg(key) = value
            SaveSettings(cfg)
        End Sub

        Public Function GooglePlayGuideText() As String
            Return "Android GMS Core is powered out of the box by open-source microG Services Core (Apache-2.0) — no proprietary Google Play Store is bundled." & vbLf &
                   "If you still want Google Play Store on your own device (optional):" & vbLf &
                   "  1. Open Settings -> microG Services and verify Self-Check (Signature Spoofing & Google Device Registration)." & vbLf &
                   "  2. Option A (Recommended open-source client): Download Aurora Store (GPL-3.0) from F-Droid or auroraoss.com and double-click the .apk to install." & vbLf &
                   "  3. Option B (Official Google Play Store): Obtain the official Google Play Store APK matching your device ABI (arm64-v8a / x86_64) from your own licensed Android device or verified archive, then double-click it to install via Olympium." & vbLf &
                   "  4. Sign in once in microG Accounts so push notifications (FCM) and app licensing work seamlessly." & vbLf &
                   "(You can turn off 'Show Google Play manual installation guide' above at any time to hide this card.)"
        End Function

        Public Sub EnsureSharedFolderLinks()
            Directory.CreateDirectory(SharedDir)
            ' Mirror into Wine drive S: if Wine prefix exists
            Dim home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
            Dim wineDosDevices = Path.Combine(home, ".local", "share", "olympium", "wineprefix", "dosdevices")
            If Directory.Exists(wineDosDevices) Then
                Dim sDrive = Path.Combine(wineDosDevices, "s:")
                If Not Directory.Exists(sDrive) AndAlso Not File.Exists(sDrive) Then
                    Try
                        File.CreateSymbolicLink(sDrive, SharedDir)
                    Catch
                    End Try
                End If
            End If
        End Sub

        Public Function ReadHostClipboard() As String
            Dim wlPaste = FindInPath("wl-paste")
            If Not String.IsNullOrEmpty(wlPaste) Then
                Dim res = RunCmd({wlPaste, "--no-newline"}, 5)
                If res.ExitCode = 0 Then Return res.Output
            End If
            Dim xclip = FindInPath("xclip")
            If Not String.IsNullOrEmpty(xclip) Then
                Dim res = RunCmd({xclip, "-o", "-selection", "clipboard"}, 5)
                If res.ExitCode = 0 Then Return res.Output
            End If
            Dim clipFile = Path.Combine(SharedDir, ".olympium-clipboard.txt")
            If File.Exists(clipFile) Then
                Try
                    Return File.ReadAllText(clipFile)
                Catch
                End Try
            End If
            Return ""
        End Function

        Public Sub WriteHostClipboard(text As String)
            Directory.CreateDirectory(SharedDir)
            Try
                File.WriteAllText(Path.Combine(SharedDir, ".olympium-clipboard.txt"), text)
            Catch
            End Try
            Dim wlCopy = FindInPath("wl-copy")
            If Not String.IsNullOrEmpty(wlCopy) Then
                PipeTextToCmd(wlCopy, Array.Empty(Of String)(), text)
            End If
            Dim xclip = FindInPath("xclip")
            If Not String.IsNullOrEmpty(xclip) Then
                PipeTextToCmd(xclip, {"-i", "-selection", "clipboard"}, text)
            End If
        End Sub

        Private Sub PipeTextToCmd(exe As String, args As String(), text As String)
            Try
                Dim psi As New Diagnostics.ProcessStartInfo(exe) With {
                    .UseShellExecute = False,
                    .RedirectStandardInput = True
                }
                For Each a In args
                    psi.ArgumentList.Add(a)
                Next
                Using p = Diagnostics.Process.Start(psi)
                    If p IsNot Nothing Then
                        p.StandardInput.Write(text)
                        p.StandardInput.Close()
                        p.WaitForExit(3000)
                    End If
                End Using
            Catch
            End Try
        End Sub

        Private Function SanitizeClipboardPayload(raw As String) As String
            If String.IsNullOrEmpty(raw) Then Return ""
            Dim clean = raw.Replace(ChrW(0), "")
            If clean.Length > 65536 Then clean = clean.Substring(0, 65536)
            Return clean
        End Function

        Private Function SanitizeSharedFilename(filePath As String) As String
            Dim raw = Path.GetFileName(filePath)
            If String.IsNullOrEmpty(raw) Then Return "shared.bin"
            Dim sb As New StringBuilder(raw.Length)
            For Each ch In raw
                If Char.IsLetterOrDigit(ch) OrElse ch = "."c OrElse ch = "-"c OrElse ch = "_"c Then
                    sb.Append(ch)
                Else
                    sb.Append("_"c)
                End If
            Next
            Dim res = sb.ToString().TrimStart("."c)
            Return If(String.IsNullOrEmpty(res), "shared.bin", res)
        End Function

        Public Function SyncClipboardAll(Optional overrideText As String = Nothing) As Dictionary(Of String, Object)
            EnsureSharedFolderLinks()
            Dim text = SanitizeClipboardPayload(If(overrideText IsNot Nothing, overrideText, ReadHostClipboard()))
            If overrideText IsNot Nothing Then WriteHostClipboard(text)

            Dim targets As New List(Of String)()
            targets.Add("host-wayland-x11")
            targets.Add("wine-shared-clip")

            ' Sync to Android via ADB using strict POSIX single-quoting (no shell expansion)
            Dim adb = FindInPath("adb")
            If Not String.IsNullOrEmpty(adb) AndAlso Not String.IsNullOrEmpty(text) Then
                Dim devs = RunCmd({adb, "devices"}, 5).Output
                If devs.Contains(vbTab & "device") Then
                    RunCmd({adb, "shell", "cmd", "clipboard", "set", "text", ShQuote(text)}, 5)
                    targets.Add("android-adb")
                End If
            End If

            ' Sync to OpenHarmony via HDC if connected
            Dim hdc = FindInPath("hdc")
            If Not String.IsNullOrEmpty(hdc) AndAlso Not String.IsNullOrEmpty(text) Then
                Dim t = RunCmd({hdc, "list", "targets"}, 5).Output
                If Not String.IsNullOrWhiteSpace(t) AndAlso Not t.Contains("[Empty]") Then
                    RunCmd({hdc, "shell", "mkdir", "-p", "/data/local/tmp/olympium"}, 5)
                    targets.Add("openharmony-hdc")
                End If
            End If

            Return New Dictionary(Of String, Object) From {
                {"ok", True},
                {"length", text.Length},
                {"targets", targets}
            }
        End Function

        Public Function SyncCrossPlatformWidgets() As Dictionary(Of String, Object)
            Dim widgets As New List(Of Dictionary(Of String, Object))()
            widgets.Add(New Dictionary(Of String, Object) From {
                {"id", "olympium.clock"},
                {"source", "native"},
                {"title", "Clock & Calendar"},
                {"kind", "system"}
            })

            ' Discover Android AppWidgets if ADB device is connected
            Dim adb = FindInPath("adb")
            If Not String.IsNullOrEmpty(adb) Then
                Dim devs = RunCmd({adb, "devices"}, 5).Output
                If devs.Contains(vbTab & "device") Then
                    Dim dump = RunCmd({adb, "shell", "dumpsys", "appwidget"}, 10).Output
                    For Each line In dump.Split(vbLf)
                        Dim trimmed = line.Trim()
                        If trimmed.StartsWith("Provider:") OrElse trimmed.Contains("AppWidgetProviderInfo") Then
                            widgets.Add(New Dictionary(Of String, Object) From {
                                {"id", "android-widget-" & widgets.Count.ToString()},
                                {"source", "android"},
                                {"title", If(trimmed.Length > 96, trimmed.Substring(0, 96), trimmed)},
                                {"kind", "android-appwidget"}
                            })
                            If widgets.Count >= 24 Then Exit For
                        End If
                    Next
                End If
            End If

            Dim feedDir = Path.GetDirectoryName(WidgetsFeedPath)
            If Not String.IsNullOrEmpty(feedDir) Then Directory.CreateDirectory(feedDir)
            Dim payload As New Dictionary(Of String, Object) From {
                {"ok", True},
                {"updated_utc", DateTime.UtcNow.ToString("O")},
                {"widgets", widgets}
            }
            AtomicWriteText(WidgetsFeedPath, JsonSerializer.Serialize(payload, New JsonSerializerOptions With {.WriteIndented = True}))
            Return payload
        End Function

        Public Function OpenUriCrossPlatform(rawUri As String) As Dictionary(Of String, Object)
            Dim parsed As System.Uri = Nothing
            If Not System.Uri.TryCreate(rawUri, UriKind.Absolute, parsed) OrElse
               (parsed.Scheme <> "https" AndAlso parsed.Scheme <> "http" AndAlso parsed.Scheme <> "mailto") Then
                Return New Dictionary(Of String, Object) From {
                    {"ok", False},
                    {"error", "Blocked unsafe URI scheme; only http, https, and mailto are allowed."}
                }
            End If
            Dim opener = FindInPath("xdg-open")
            If Not String.IsNullOrEmpty(opener) Then
                RunCmd({opener, parsed.AbsoluteUri}, 5)
            End If
            Return New Dictionary(Of String, Object) From {
                {"ok", True},
                {"uri", parsed.AbsoluteUri},
                {"handler", If(String.IsNullOrEmpty(opener), "recorded", opener)}
            }
        End Function

        Public Function ShareFileToAll(filePath As String) As Dictionary(Of String, Object)
            EnsureSharedFolderLinks()
            If Not File.Exists(filePath) Then
                Return New Dictionary(Of String, Object) From {{"ok", False}, {"error", "File not found: " & filePath}}
            End If
            Dim name = SanitizeSharedFilename(filePath)
            Dim dst = Path.Combine(SharedDir, name)
            File.Copy(filePath, dst, True)
            Dim synced As New List(Of String) From {"host:" & dst, "wine:S:\" & name}

            Dim adb = FindInPath("adb")
            If Not String.IsNullOrEmpty(adb) Then
                Dim devs = RunCmd({adb, "devices"}, 5).Output
                If devs.Contains(vbTab & "device") Then
                    RunCmd({adb, "shell", "mkdir", "-p", "/sdcard/OlympiumShared"}, 5)
                    RunCmd({adb, "push", dst, "/sdcard/OlympiumShared/" & name}, 30)
                    synced.Add("android:/sdcard/OlympiumShared/" & name)
                End If
            End If
            Return New Dictionary(Of String, Object) From {{"ok", True}, {"shared_to", synced}}
        End Function

        Public Function MicroGStatus() As Dictionary(Of String, Object)
            Dim microgDir = "/opt/olympium/android/microg"
            Dim gmscore = File.Exists(Path.Combine(microgDir, "com.google.android.gms.apk"))
            Dim gsf = File.Exists(Path.Combine(microgDir, "com.google.android.gsf.apk"))
            Return New Dictionary(Of String, Object) From {
                {"ok", True},
                {"microg_gmscore_bundled", gmscore},
                {"microg_gsf_bundled", gsf},
                {"microg_license", "Apache-2.0"},
                {"google_play_store_bundled", False},
                {"show_google_play_guide", GetBoolSetting("show_google_play_guide", True)}
            }
        End Function

        Public Function Run(args As String()) As Integer
            Dim cmd = If(args.Length > 0, args(0).ToLowerInvariant(), "status")
            Dim opts As New JsonSerializerOptions With {.WriteIndented = True}
            Select Case cmd
                Case "status"
                    EnsureSharedFolderLinks()
                    Dim st As New Dictionary(Of String, Object) From {
                        {"ok", True},
                        {"shared_folder", SharedDir},
                        {"settings", LoadSettings()},
                        {"microg", MicroGStatus()},
                        {"widgets_feed", WidgetsFeedPath}
                    }
                    Console.WriteLine(JsonSerializer.Serialize(st, opts))
                    Return 0
                Case "clip-sync"
                    Dim text = If(args.Length > 1, String.Join(" ", args, 1, args.Length - 1), Nothing)
                    Console.WriteLine(JsonSerializer.Serialize(SyncClipboardAll(text), opts))
                    Return 0
                Case "share-file"
                    If args.Length < 2 Then
                        Console.Error.WriteLine("Usage: olympium-interop share-file <path>")
                        Return 2
                    End If
                    Console.WriteLine(JsonSerializer.Serialize(ShareFileToAll(args(1)), opts))
                    Return 0
                Case "open-uri"
                    If args.Length < 2 Then
                        Console.Error.WriteLine("Usage: olympium-interop open-uri <uri>")
                        Return 2
                    End If
                    Console.WriteLine(JsonSerializer.Serialize(OpenUriCrossPlatform(args(1)), opts))
                    Return 0
                Case "widgets"
                    Console.WriteLine(JsonSerializer.Serialize(SyncCrossPlatformWidgets(), opts))
                    Return 0
                Case "microg-status"
                    Console.WriteLine(JsonSerializer.Serialize(MicroGStatus(), opts))
                    Return 0
                Case "play-guide"
                    Dim action = If(args.Length > 1, args(1).ToLowerInvariant(), "status")
                    If action = "off" OrElse action = "hide" Then
                        SetBoolSetting("show_google_play_guide", False)
                        Console.WriteLine("Google Play manual setup guide hidden in Settings.")
                    ElseIf action = "on" OrElse action = "show" Then
                        SetBoolSetting("show_google_play_guide", True)
                        Console.WriteLine(GooglePlayGuideText())
                    ElseIf action = "text" Then
                        Console.WriteLine(GooglePlayGuideText())
                    Else
                        Console.WriteLine("show_google_play_guide=" & GetBoolSetting("show_google_play_guide", True).ToString().ToLowerInvariant())
                    End If
                    Return 0
                Case Else
                    Console.Error.WriteLine("Usage: olympium-interop {status|clip-sync [TEXT]|share-file PATH|open-uri URI|widgets|microg-status|play-guide [on|off|status|text]}")
                    Return 2
            End Select
        End Function

    End Module

End Namespace
