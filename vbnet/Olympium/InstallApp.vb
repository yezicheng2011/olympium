' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Unified package review, installation, and launcher (dev-install) in VB.NET (.NET 8).
' Handles Linux ELF/scripts, .deb, .rpm, Windows .exe (automatic Wine),
' Mac .dmg/.pkg/.app (confined via libolympium-native.so + Darling),
' Android .apk (olympium-android), and OpenHarmony .hap (olympium-openharmony).

Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Text
Imports System.Text.Json

Namespace Olympium

    Public Class InstallReview
        Public FilePath As String = ""
        Public Name As String = ""
        Public Guest As String = ""
        Public Kind As String = ""
        Public Detail As String = ""
        Public Message As String = ""
        Public Program As String = ""
        Public LaunchType As String = ""
        Public Engine As String = ""
        Public Id As String = ""
        Public RuntimePresent As Boolean = False
        Public PlanOk As Boolean = False
        Public Hides As Boolean = False
        Public NativeMode As Boolean = False
        Public HostWindow As Boolean = False
        Public Launchable As Boolean = False
        Public CacheHit As Boolean = False
        Public Action As String = "refuse"
        Public MacApps As Integer = 0
        Public MacTitle As String = ""
        Public MacIcon As String = ""
        Public MacFirst As String = ""
    End Class

    Public Class LibraryRecord
        Public Id As String = ""
        Public Name As String = ""
        Public Path As String = ""
        Public Sha As String = ""
        Public Guest As String = ""
        Public Kind As String = ""
        Public Launch As String = ""
        Public Ext As String = ""
        Public Program As String = ""
    End Class

    Public Module InstallApp

        Private Function RuntimeBin() As String
            Dim env = Environment.GetEnvironmentVariable("OLYMPIUM_BIN")
            If Not String.IsNullOrEmpty(env) AndAlso IsExecutable(env) Then Return env
            If IsExecutable("/usr/bin/olympium") Then Return "/usr/bin/olympium"
            Return ""
        End Function

        Private Function MobileRuntime(openHarmony As Boolean) As String
            Dim envName = If(openHarmony, "OLYMPIUM_OPENHARMONY_RUNTIME", "OLYMPIUM_ANDROID_RUNTIME")
            Dim env = Environment.GetEnvironmentVariable(envName)
            If Not String.IsNullOrEmpty(env) AndAlso env.StartsWith("/") AndAlso IsExecutable(env) Then Return env
            Dim installed = If(openHarmony, "/usr/libexec/dev-desktop/olympium-openharmony", "/usr/libexec/dev-desktop/olympium-android")
            Return If(IsExecutable(installed), installed, "")
        End Function

        Private Function MacRuntimeCheck(ByRef helperPath As String) As Boolean
            Dim env = Environment.GetEnvironmentVariable("OLYMPIUM_MAC_RUNTIME")
            helperPath = If(Not String.IsNullOrEmpty(env), env, "/usr/libexec/dev-desktop/olympium-mac-run")
            If Not IsExecutable(helperPath) Then Return False
            Dim res = RunCmd({helperPath, "--check"}, 10)
            Return res.ExitCode = 0
        End Function

        Private Function LibraryFile() As String
            Return Path.Combine(DeskRoot(), "library.tsv")
        End Function

        Private Function LoadRecords() As List(Of LibraryRecord)
            Dim list As New List(Of LibraryRecord)()
            Dim p = LibraryFile()
            If Not File.Exists(p) Then Return list
            For Each line In File.ReadAllLines(p)
                If String.IsNullOrWhiteSpace(line) Then Continue For
                Dim cols = line.Split(vbTab)
                If cols.Length < 7 Then Continue For
                Dim rec As New LibraryRecord With {
                    .Id = cols(0),
                    .Name = cols(1),
                    .Path = cols(2),
                    .Sha = cols(3),
                    .Guest = cols(4),
                    .Kind = cols(5),
                    .Launch = cols(6),
                    .Ext = If(cols.Length > 7 AndAlso cols(7) <> "-", cols(7), ""),
                    .Program = If(cols.Length > 8 AndAlso cols(8) <> "-", cols(8), "")
                }
                list.Add(rec)
            Next
            Return list
        End Function

        Private Function SanitizeDesktopField(raw As String) As String
            If String.IsNullOrEmpty(raw) Then Return ""
            Dim sb As New StringBuilder(raw.Length)
            For Each ch In raw
                Dim c = AscW(ch)
                If c >= &H20 AndAlso c <> &H7F Then sb.Append(ch)
            Next
            Return sb.ToString().Replace(vbTab, " ").Trim()
        End Function

        Private Sub SaveRecords(recs As List(Of LibraryRecord))
            Dim sb As New StringBuilder()
            For Each r In recs
                sb.Append(SanitizeDesktopField(r.Id)).Append(vbTab)
                sb.Append(SanitizeDesktopField(r.Name)).Append(vbTab)
                sb.Append(SanitizeDesktopField(r.Path)).Append(vbTab)
                sb.Append(SanitizeDesktopField(r.Sha)).Append(vbTab)
                sb.Append(SanitizeDesktopField(r.Guest)).Append(vbTab)
                sb.Append(SanitizeDesktopField(r.Kind)).Append(vbTab)
                sb.Append(SanitizeDesktopField(r.Launch)).Append(vbTab)
                sb.Append(If(String.IsNullOrEmpty(r.Ext), "-", SanitizeDesktopField(r.Ext))).Append(vbTab)
                sb.Append(If(String.IsNullOrEmpty(r.Program), "-", SanitizeDesktopField(r.Program))).Append(vbLf)
            Next
            AtomicWriteText(LibraryFile(), sb.ToString())
        End Sub

        Private Sub WriteLauncherFor(rec As LibraryRecord, iconPath As String, pinToDock As Boolean)
            Dim safeId = SanitizeDesktopField(rec.Id).Replace("/", "-").Replace("..", "-")
            Dim safeName = SanitizeDesktopField(rec.Name)
            If String.IsNullOrEmpty(safeName) Then safeName = safeId
            Dim safeIcon = SanitizeDesktopField(iconPath)

            Dim root = DeskRoot()
            Dim launchDir = Path.Combine(root, "launch")
            Directory.CreateDirectory(launchDir)
            Dim wrap = Path.Combine(launchDir, safeId)
            Dim selfBin = If(IsExecutable("/usr/bin/dev-install"), "/usr/bin/dev-install", "dev-install")
            Dim wrapContent = $"#!/bin/sh{vbLf}exec {ShQuote(selfBin)} launch {ShQuote(safeId)} ""$@""{vbLf}"
            AtomicWriteText(wrap, wrapContent, &H1EDUI) ' 0755

            Dim apps = AppsDir()
            Directory.CreateDirectory(apps)
            Dim desk = Path.Combine(apps, $"olympium-{safeId}.desktop")
            Dim iconLine = If(Not String.IsNullOrEmpty(safeIcon), $"Icon={safeIcon}{vbLf}", "Icon=olympium" & vbLf)
            Dim deskContent =
                "[Desktop Entry]" & vbLf &
                "Type=Application" & vbLf &
                $"Name={safeName}" & vbLf &
                $"Exec={wrap} %f" & vbLf &
                iconLine &
                "Terminal=false" & vbLf &
                "Categories=Utility;" & vbLf
            AtomicWriteText(desk, deskContent)

            If pinToDock Then
                Dim pinnedPath = Path.Combine(root, "pinned")
                Dim lines As New List(Of String)()
                If File.Exists(pinnedPath) Then lines.AddRange(File.ReadAllLines(pinnedPath))
                Dim entry = $"{safeName}{vbTab}{wrap}"
                If Not lines.Any(Function(l) l.EndsWith(vbTab & wrap)) Then
                    lines.Add(entry)
                    AtomicWriteText(pinnedPath, String.Join(vbLf, lines) & vbLf)
                End If
            End If
        End Sub

        Private Sub LocalKind(path As String, r As InstallReview)
            If Not File.Exists(path) Then Return
            Dim mag(127) As Byte
            Dim n As Integer
            Using fs = File.OpenRead(path)
                n = fs.Read(mag, 0, mag.Length)
            End Using
            Dim execBit = IsExecutable(path)
            If n >= 4 AndAlso mag(0) = &H7F AndAlso mag(1) = AscW("E"c) AndAlso mag(2) = AscW("L"c) AndAlso mag(3) = AscW("F"c) AndAlso execBit Then
                r.Guest = "linux" : r.Kind = "linux_executable" : r.Detail = "Linux ELF program."
                r.Program = path : r.LaunchType = "process" : r.Hides = True : r.NativeMode = True : r.PlanOk = True
            ElseIf n >= 2 AndAlso mag(0) = AscW("#"c) AndAlso mag(1) = AscW("!"c) AndAlso execBit Then
                r.Guest = "linux" : r.Kind = "linux_script" : r.Detail = "Script with a shebang."
                r.Program = path : r.LaunchType = "process" : r.Hides = True : r.NativeMode = True : r.PlanOk = True
            ElseIf n >= 8 AndAlso Encoding.ASCII.GetString(mag, 0, 8) = "!<arch>" & vbLf Then
                r.Guest = "linux" : r.Kind = "linux_deb" : r.Detail = "Debian package."
            ElseIf n >= 4 AndAlso mag(0) = &HED AndAlso mag(1) = &HAB AndAlso mag(2) = &HEE AndAlso mag(3) = &HDB Then
                r.Guest = "linux" : r.Kind = "linux_rpm" : r.Detail = "RPM package."
            ElseIf n >= 2 AndAlso mag(0) = AscW("M"c) AndAlso mag(1) = AscW("Z"c) AndAlso IsRealPe(path) Then
                r.Guest = "windows" : r.Kind = "windows_exe" : r.Detail = "Windows program."
                r.Engine = "wine" : r.LaunchType = "process" : r.Program = "/usr/bin/wine"
                r.Hides = True : r.PlanOk = True
                If Not IsExecutable(r.Program) Then
                    r.Message = "Windows program. Wine is not installed on this system, so it cannot be started here."
                End If
            ElseIf path.EndsWith(".msi", StringComparison.OrdinalIgnoreCase) Then
                r.Guest = "windows" : r.Kind = "windows_msi" : r.Detail = "Windows installer package."
                r.Engine = "wine" : r.LaunchType = "process" : r.Program = "/usr/bin/wine"
                r.Hides = True : r.PlanOk = True
                If Not IsExecutable(r.Program) Then
                    r.Message = "Windows installer package. Wine is not installed on this system."
                End If
            End If
        End Sub

        Private Function ReviewMac(r As InstallReview) As Boolean
            Dim kind = NativeBridge.MacDetect(r.FilePath)
            If String.IsNullOrEmpty(kind) Then Return False
            Dim cacheDir = Path.Combine(DeskRoot(), "cache", BytesDigestSha256(r.FilePath).Substring(0, 16) & ".mac")
            Directory.CreateDirectory(cacheDir)
            Dim jsonBuf As New StringBuilder(16384)
            If NativeBridge.olympium_mac_review_json(r.FilePath, cacheDir, jsonBuf, New UIntPtr(16384UI)) <> 0 Then
                Return False
            End If
            Using doc = JsonDocument.Parse(jsonBuf.ToString())
                Dim root = doc.RootElement
                r.Guest = "macos"
                r.Kind = kind
                r.Engine = "mac"
                r.RuntimePresent = Not String.IsNullOrEmpty(RuntimeBin())
                r.MacTitle = root.GetProperty("title").GetString()
                Dim apps = root.GetProperty("apps")
                r.MacApps = apps.GetArrayLength()
                Dim what = If(kind = "mac_dmg", "Mac disk image", If(kind = "mac_pkg", "Mac installer package", "Mac application"))
                r.Detail = what
                If r.MacApps = 0 Then
                    r.Message = $"{what}. {root.GetProperty("note").GetString()}"
                    Return True
                End If
                Dim first = apps(0)
                r.MacIcon = first.GetProperty("icon").GetString()
                r.MacFirst = first.GetProperty("name").GetString()
                Dim names As New List(Of String)()
                For Each a In apps.EnumerateArray()
                    Dim nm = a.GetProperty("name").GetString()
                    Dim ver = a.GetProperty("version").GetString()
                    names.Add(If(String.IsNullOrEmpty(ver), nm, $"{nm} {ver}"))
                Next
                Dim helper = ""
                Dim opens = If(MacRuntimeCheck(helper),
                    "It opens through the Mac runtime on this system.",
                    "Opening it needs the Darling runtime, which is not on this system.")
                If kind = "mac_app" Then
                    Dim archs = first.GetProperty("archs").GetString()
                    r.Message = $"Mac application {String.Join(", ", names)}{If(String.IsNullOrEmpty(archs), "", ", built for " & archs)}. Install copies it into Applications on this desktop. It is not run. {opens}"
                Else
                    Dim plural = If(r.MacApps = 1, "", "s")
                    Dim pron = If(r.MacApps = 1, "it", "them")
                    Dim scripts = If(kind = "mac_pkg", ", and the package's installer scripts are not used", "")
                    r.Message = $"{what}. It holds {r.MacApps} application{plural}: {String.Join(", ", names)}. Install copies {pron} into Applications on this desktop. Nothing in it is run{scripts}. {opens}"
                End If
            End Using
            Return True
        End Function

        Private Sub Decide(r As InstallReview)
            r.Launchable = False
            r.Action = "refuse"
            If r.Kind = "android_apk" AndAlso r.Engine = "android" Then
                r.Action = If(r.PlanOk, "android", "review")
                Return
            End If
            If r.Kind = "openharmony_hap" AndAlso r.Engine = "openharmony" Then
                r.Action = If(r.PlanOk, "openharmony", "review")
                Return
            End If
            If r.Kind.StartsWith("mac_") AndAlso r.Engine = "mac" Then
                r.Action = If(r.MacApps > 0, "mac", "review")
                Return
            End If
            If r.Guest = "ios" Then
                r.Message = "iOS is not part of this system."
                Return
            End If
            Dim linuxProg = r.Guest = "linux" AndAlso (r.Kind = "linux_executable" OrElse r.Kind = "linux_script")
            If linuxProg AndAlso r.PlanOk AndAlso r.Hides AndAlso r.NativeMode AndAlso r.LaunchType = "process" AndAlso IsExecutable(r.Program) Then
                r.Launchable = True
                r.Action = "import"
                If String.IsNullOrEmpty(r.Message) Then r.Message = "Runs as its own window on this desktop. Nothing is downloaded."
                Return
            End If
            If r.Kind = "linux_deb" Then
                Dim res = RunCmd({"/usr/bin/dpkg-deb", "-f", r.FilePath, "Package"}, 15)
                Dim pkg = res.Output.Trim()
                If res.ExitCode = 0 AndAlso Not String.IsNullOrEmpty(pkg) Then
                    r.Action = "install"
                    r.Message = $"Debian package {pkg}. Import extracts it for this user and does not run it. Launch opens its window."
                    Return
                End If
            End If
            If (r.Kind = "windows_exe" OrElse r.Kind = "windows_msi") AndAlso r.PlanOk AndAlso r.Engine = "wine" AndAlso IsExecutable(r.Program) Then
                If r.Kind = "windows_exe" AndAlso IsRealPe(r.FilePath) Then
                    Dim why = ""
                    If Not WineAutoRunner.CanRunMachine(ReadPeMachine(r.FilePath), why) Then
                        r.Action = "review"
                        r.Message = why
                        Return
                    End If
                End If
                r.Action = "wine"
                r.Launchable = True
                r.Message = "Windows program. Runs automatically through Wine in its own window on this desktop."
                Return
            End If
            If r.Kind = "linux_rpm" AndAlso IsExecutable("/usr/bin/rpm") AndAlso IsExecutable("/usr/bin/rpm2cpio") Then
                Dim res = RunCmd({"/usr/bin/rpm", "-qp", "--queryformat", "%{NAME}", r.FilePath}, 15)
                Dim nm = res.Output.Trim()
                If res.ExitCode = 0 AndAlso Not String.IsNullOrEmpty(nm) Then
                    r.Action = "rpm"
                    r.Message = $"RPM package {nm}. Import extracts it with rpm2cpio for this user and does not run scripts."
                    Return
                End If
            End If
            If r.Guest = "macos" Then
                r.Action = "keep"
                If String.IsNullOrEmpty(r.Message) Then r.Message = "The file can be kept in the library. It will not be started."
                Return
            End If
            If Not String.IsNullOrEmpty(r.Kind) Then
                r.Action = "review"
                If String.IsNullOrEmpty(r.Message) Then r.Message = "Recognized. This desktop has not tested an install of this format, so it will not run it."
                Return
            End If
            r.Message = "This file is not a supported package."
        End Sub

        Public Function FillReview(path As String) As InstallReview
            Dim r As New InstallReview()
            r.FilePath = RealPath(path)
            r.Name = System.IO.Path.GetFileName(r.FilePath)
            Dim ext = System.IO.Path.GetExtension(r.FilePath).ToLowerInvariant()
            If ext = ".apk" OrElse ext = ".hap" Then
                Dim isOh = (ext = ".hap")
                r.Guest = If(isOh, "openharmony", "android")
                r.Kind = If(isOh, "openharmony_hap", "android_apk")
                r.Engine = r.Guest
                r.Detail = If(isOh, "OpenHarmony application", "Android application")
                Dim helper = MobileRuntime(isOh)
                If Not String.IsNullOrEmpty(helper) Then
                    Dim res = RunCmd({helper, "inspect", r.FilePath, "--root", DeskRoot()}, 60)
                    Try
                        Using doc = JsonDocument.Parse(res.Output.Trim())
                            Dim root = doc.RootElement
                            If root.TryGetProperty("ok", Nothing) AndAlso root.GetProperty("ok").GetBoolean() Then
                                r.PlanOk = True
                                r.Launchable = True
                                If root.TryGetProperty("id", Nothing) Then r.Id = root.GetProperty("id").GetString()
                                If root.TryGetProperty("name", Nothing) Then r.MacFirst = root.GetProperty("name").GetString()
                                If root.TryGetProperty("icon", Nothing) Then r.MacIcon = root.GetProperty("icon").GetString()
                                r.MacTitle = r.MacFirst
                                r.Message = If(isOh,
                                    "OpenHarmony application. Installs into the bundled local OpenHarmony 6.1 SDK session and opens in its own window.",
                                    "Android application. Installs into the bundled Android 17 + microG runtime and opens in its own window via scrcpy.")
                            ElseIf root.TryGetProperty("message", Nothing) Then
                                r.Message = root.GetProperty("message").GetString()
                            End If
                        End Using
                    Catch
                        r.Message = If(String.IsNullOrEmpty(res.Output), "Could not inspect package.", res.Output.Trim())
                    End Try
                Else
                    r.Message = If(isOh, "The OpenHarmony runtime helper is not installed.", "The Android runtime helper is not installed.")
                End If
                Decide(r)
                Return r
            End If

            If ReviewMac(r) Then
                Decide(r)
                Return r
            End If

            LocalKind(r.FilePath, r)
            If String.IsNullOrEmpty(r.Message) AndAlso String.IsNullOrEmpty(RuntimeBin()) Then
                r.Message = "The Olympium runtime is not on this image, so this desktop read the file itself."
            End If
            Decide(r)
            Return r
        End Function

        Private Sub PrintReview(r As InstallReview)
            Dim obj = New Dictionary(Of String, Object) From {
                {"file", r.FilePath},
                {"guest", r.Guest},
                {"kind", r.Kind},
                {"detail", r.Detail},
                {"action", r.Action},
                {"launchable", r.Launchable},
                {"launch_type", r.LaunchType},
                {"runtime", If(r.RuntimePresent, "present", "absent")},
                {"cache", If(r.CacheHit, "hit", "miss")},
                {"download", "none"},
                {"ran", False},
                {"message", r.Message}
            }
            Console.WriteLine(JsonSerializer.Serialize(obj, New JsonSerializerOptions With {.WriteIndented = True}))
        End Sub

        Private Function ExtractPackage(r As InstallReview, id As String, isRpm As Boolean, rec As LibraryRecord) As Integer
            Dim root = DeskRoot()
            Dim dest = Path.Combine(root, "pkgs", id)
            If Directory.Exists(dest) Then Directory.Delete(dest, True)
            Directory.CreateDirectory(dest)
            Dim rc As Integer
            If isRpm Then
                rc = NativeBridge.RunConfinedPipe({"/usr/bin/rpm2cpio", r.FilePath}, r.FilePath,
                                                  {"/bin/cpio", "-idm", "--no-absolute-filenames", "--quiet"}, dest)
            Else
                rc = NativeBridge.RunConfined({"/usr/bin/dpkg-deb", "-x", r.FilePath, dest}, r.FilePath, dest)
            End If
            If rc <> 0 Then
                Console.Error.WriteLine($"dev-install: confined extraction failed (exit {rc})")
                Return 1
            End If
            NativeBridge.olympium_tree_settle(dest)

            ' Find an executable entry point inside usr/bin or bin
            For Each subDir In {"usr/bin", "bin", "usr/local/bin"}
                Dim fullSub = Path.Combine(dest, subDir)
                If Directory.Exists(fullSub) Then
                    For Each f In Directory.GetFiles(fullSub)
                        If IsExecutable(f) Then
                            rec.Program = f
                            Return 0
                        End If
                    Next
                End If
            Next
            Console.Error.WriteLine("dev-install: extracted package has no executable in bin/ or usr/bin/")
            Return 3
        End Function

        Private Function CmdImportMode(path As String, ext As String, consent As Boolean, updateOnly As Boolean) As Integer
            Dim r = FillReview(path)
            If updateOnly AndAlso r.Action <> "android" Then
                Console.Error.WriteLine("dev-install: managed update is currently available for Android APKs")
                Return 3
            End If
            If r.Action = "android" OrElse r.Action = "openharmony" Then
                If Not consent Then
                    Console.Error.WriteLine("dev-install: refused to install without consent")
                    Return 2
                End If
                Dim isOh = (r.Action = "openharmony")
                Dim helper = MobileRuntime(isOh)
                Console.WriteLine("progress 1/3 read")
                Console.WriteLine("progress 2/3 install")
                Console.Out.Flush()
                Dim res = RunCmd({helper, If(updateOnly, "update", "install"), r.FilePath, "--root", DeskRoot()}, 900)
                Try
                    Using doc = JsonDocument.Parse(res.Output.Trim())
                        Dim root = doc.RootElement
                        If res.ExitCode <> 0 OrElse Not root.GetProperty("ok").GetBoolean() Then
                            Dim msg = If(root.TryGetProperty("message", Nothing), root.GetProperty("message").GetString(), "Application installation failed")
                            Console.Error.WriteLine($"dev-install: not installed: {msg}")
                            Return 3
                        End If
                        Dim rec As New LibraryRecord With {
                            .Id = root.GetProperty("id").GetString(),
                            .Name = root.GetProperty("name").GetString(),
                            .Path = root.GetProperty("path").GetString(),
                            .Sha = root.GetProperty("sha").GetString(),
                            .Program = root.GetProperty("package").GetString(),
                            .Guest = r.Guest,
                            .Kind = r.Kind,
                            .Launch = r.Action
                        }
                        Dim icon = If(root.TryGetProperty("icon", Nothing), root.GetProperty("icon").GetString(), "")
                        Dim recs = LoadRecords()
                        recs.RemoveAll(Function(x) x.Id = rec.Id)
                        recs.Add(rec)
                        SaveRecords(recs)
                        WriteLauncherFor(rec, icon, False)
                        Console.WriteLine($"id={rec.Id}")
                        Console.WriteLine($"launch={rec.Launch}")
                        Console.WriteLine("progress 3/3 done")
                        Return 0
                    End Using
                Catch ex As Exception
                    Console.Error.WriteLine($"dev-install: not installed: {ex.Message}")
                    Return 3
                End Try
            End If

            If r.Action = "mac" Then
                If Not consent Then
                    Console.Error.WriteLine("dev-install: refused to install without consent")
                    PrintReview(r)
                    Return 2
                End If
                Console.WriteLine($"confinement={NativeBridge.ConfineName()}")
                Dim jsonBuf As New StringBuilder(16384)
                Dim errBuf As New StringBuilder(512)
                Dim count = NativeBridge.olympium_mac_install_json(r.FilePath, DeskRoot(), jsonBuf, New UIntPtr(16384UI), errBuf, New UIntPtr(512UI))
                If count < 0 Then
                    Console.Error.WriteLine($"dev-install: not installed: {errBuf}")
                    Console.WriteLine("download: none")
                    Return 1
                End If
                Dim recs = LoadRecords()
                Using doc = JsonDocument.Parse(jsonBuf.ToString())
                    For Each app In doc.RootElement.GetProperty("apps").EnumerateArray()
                        Dim prog = app.GetProperty("program").GetString()
                        Dim bundle = app.GetProperty("bundle").GetString()
                        Dim sha = FileDigest(prog, "sha256")
                        Dim id = "mac-" & BytesDigestSha256(bundle).Substring(0, 8)
                        Dim rec As New LibraryRecord With {
                            .Id = id,
                            .Name = app.GetProperty("name").GetString(),
                            .Path = bundle,
                            .Sha = sha,
                            .Guest = "macos",
                            .Kind = "mac_app",
                            .Launch = "mac",
                            .Program = prog
                        }
                        recs.RemoveAll(Function(x) x.Path = bundle)
                        recs.Add(rec)
                        SaveRecords(recs)
                        Dim icon = app.GetProperty("icon").GetString()
                        Console.WriteLine($"id={rec.Id}")
                        Console.WriteLine($"app={rec.Name}")
                        Console.WriteLine($"installed={rec.Path}")
                        Console.WriteLine($"launch=mac")
                        WriteLauncherFor(rec, icon, False)
                    Next
                End Using
                Console.WriteLine($"installed_count={count}")
                Console.WriteLine("scripts_run=0")
                Console.WriteLine("download: none")
                Return 0
            End If

            Console.WriteLine("progress 1/4 classify")
            Console.WriteLine("progress 2/4 plan")
            If Not consent Then
                Console.Error.WriteLine("dev-install: refused to import without consent")
                PrintReview(r)
                Return 2
            End If
            If r.Action <> "import" AndAlso r.Action <> "keep" AndAlso r.Action <> "install" AndAlso r.Action <> "rpm" AndAlso r.Action <> "wine" Then
                Console.Error.WriteLine($"dev-install: not installed: {r.Message}")
                PrintReview(r)
                Return 3
            End If

            Dim fileSha = FileDigest(r.FilePath, "sha256")
            Dim newId = If(r.Action = "wine", "wine-" & fileSha.Substring(0, 8), "local-" & fileSha.Substring(0, 12))
            Console.WriteLine($"progress 3/4 {If(r.Action = "install" OrElse r.Action = "rpm", "extract", "checksum")}")
            Dim newRec As New LibraryRecord With {
                .Id = newId,
                .Name = r.Name,
                .Path = r.FilePath,
                .Sha = fileSha,
                .Guest = r.Guest,
                .Kind = r.Kind,
                .Launch = If(r.Action = "wine", "wine", If(r.Launchable, "native", "refused")),
                .Ext = If(ext, ""),
                .Program = r.Program
            }
            If r.Action = "install" OrElse r.Action = "rpm" Then
                Dim ec = ExtractPackage(r, newId, r.Action = "rpm", newRec)
                If ec <> 0 Then Return ec
                newRec.Launch = "native"
            End If
            Dim allRecs = LoadRecords()
            allRecs.RemoveAll(Function(x) x.Path = r.FilePath)
            allRecs.Add(newRec)
            SaveRecords(allRecs)
            Console.WriteLine($"id={newRec.Id}")
            Console.WriteLine($"launch={newRec.Launch}")
            If newRec.Launch = "native" OrElse newRec.Launch = "wine" Then
                WriteLauncherFor(newRec, "", True)
            End If
            Console.WriteLine("progress 4/4 desktop")
            Console.WriteLine("download: none")
            Return 0
        End Function

        Private Function CmdLaunch(id As String, extraArgs As String()) As Integer
            Dim recs = LoadRecords()
            Dim rec = recs.FirstOrDefault(Function(x) x.Id = id)
            If rec Is Nothing Then
                Console.Error.WriteLine($"dev-install: unknown id {id}")
                Return 1
            End If
            Dim checkFile = If(rec.Launch = "mac", rec.Program, rec.Path)
            If Not File.Exists(checkFile) OrElse FileDigest(checkFile, "sha256") <> rec.Sha Then
                Console.Error.WriteLine("dev-install: checksum changed; not started")
                Return 4
            End If
            If rec.Launch = "wine" Then
                Return WineAutoRunner.LaunchExe(rec.Path, extraArgs, emitMarks:=True)
            End If
            If rec.Launch = "android" OrElse rec.Launch = "openharmony" Then
                Dim isOh = (rec.Launch = "openharmony")
                Dim helper = MobileRuntime(isOh)
                If String.IsNullOrEmpty(helper) Then
                    Console.Error.WriteLine($"dev-install: {rec.Launch} runtime is not installed")
                    Return 3
                End If
                Dim argv As New List(Of String) From {helper, "launch", rec.Program, "--root", DeskRoot()}
                If isOh Then
                    argv.Add("--title") : argv.Add(rec.Name)
                End If
                Return RunPassthrough(argv)
            End If
            If rec.Launch = "mac" Then
                Dim helper = ""
                If Not MacRuntimeCheck(helper) Then
                    Console.Error.WriteLine($"dev-install: Darling is not installed for {rec.Name}")
                    Return 3
                End If
                Dim argv As New List(Of String) From {helper, rec.Path, rec.Program}
                argv.AddRange(extraArgs)
                Return RunPassthrough(argv)
            End If
            If rec.Launch = "native" Then
                Dim target = If(Not String.IsNullOrEmpty(rec.Program), rec.Program, rec.Path)
                Dim runner = EnvOr("OLYMPIUM_RUN", "/usr/libexec/dev-desktop/olympium-run")
                Dim argv As New List(Of String)()
                If IsElf(target) AndAlso IsExecutable(runner) Then argv.Add(runner)
                argv.Add(target)
                argv.AddRange(extraArgs)
                Return RunPassthrough(argv)
            End If
            Console.Error.WriteLine($"dev-install: {rec.Name} is in the library and is not started")
            Return 3
        End Function

        Private Function CmdPrewarm(what As String) As Integer
            Dim flag = Path.Combine(DeskRoot(), "prewarm")
            Select Case what
                Case "on"
                    Directory.CreateDirectory(DeskRoot())
                    File.WriteAllText(flag, "on" & vbLf)
                    Console.WriteLine("prewarm on")
                    Return 0
                Case "off"
                    If File.Exists(flag) Then File.Delete(flag)
                    If IsExecutable("/usr/bin/wineserver") Then
                        RunCmd({"/usr/bin/wineserver", "-k"}, 10, New Dictionary(Of String, String) From {{"WINEPREFIX", WineAutoRunner.WinePrefixPath()}})
                    End If
                    Console.WriteLine("prewarm off")
                    Return 0
                Case "status"
                    Console.WriteLine("prewarm " & If(File.Exists(flag), "on", "off"))
                    Return 0
                Case "run"
                    ' Ensure binfmt_misc for Windows .exe is active at session start.
                    WineAutoRunner.EnsureBinfmtRegistered()
                    If Not File.Exists(flag) Then
                        Console.WriteLine("prewarm off; nothing started")
                        Return 0
                    End If
                    If Not IsExecutable("/usr/bin/wine") Then
                        Console.WriteLine("prewarm: Wine is not installed; nothing started")
                        Return 0
                    End If
                    Dim prefix = WineAutoRunner.WinePrefixPath()
                    Dim ms As Long = 0
                    Dim st = WineAutoRunner.PreparePrefix(prefix, ms)
                    Dim sw = Stopwatch.StartNew()
                    WineAutoRunner.KeepWineServer(prefix, 600)
                    RunCmd({"/usr/bin/wine", "cmd", "/c", "exit"}, 30, New Dictionary(Of String, String) From {
                        {"WINEPREFIX", prefix}, {"WINEDLLOVERRIDES", "mscoree,mshtml="}, {"WINEDEBUG", "-all"}
                    })
                    Console.WriteLine($"prewarm wine_prefix {st}")
                    Console.WriteLine($"prewarm wine_prefix_ms {ms}")
                    Console.WriteLine($"prewarm wine_server_ms {sw.ElapsedMilliseconds}")
                    Return 0
                Case Else
                    Return 2
            End Select
        End Function

        Private Function CmdOpen(path As String, noUi As Boolean) As Integer
            Dim resolved = RealPath(path)
            Dim existing = LoadRecords().FirstOrDefault(Function(x) x.Path = resolved)
            Dim forceDialog = Not String.IsNullOrEmpty(Environment.GetEnvironmentVariable("OLYMPIUM_INSTALL_FORCE_DIALOG"))

            ' If already installed in library, double-clicking launches it immediately!
            If existing IsNot Nothing AndAlso Not noUi AndAlso Not forceDialog AndAlso existing.Launch <> "refused" Then
                Return CmdLaunch(existing.Id, Array.Empty(Of String)())
            End If

            Dim r = FillReview(resolved)
            Dim hasGui = Not String.IsNullOrEmpty(Environment.GetEnvironmentVariable("DISPLAY")) OrElse
                         Not String.IsNullOrEmpty(Environment.GetEnvironmentVariable("WAYLAND_DISPLAY"))

            ' Seamless one-step double-click execution/installation for EVERY supported format!
            If Not noUi AndAlso Not forceDialog Then
                ' 1. Windows .exe / .msi -> direct transparent Wine launch.
                If r.Action = "wine" Then
                    Return WineAutoRunner.LaunchExe(r.FilePath, Array.Empty(Of String)(), emitMarks:=False)
                End If
                ' 2. Native or cross-arch Linux ELF uses olympium-run. A shebang script runs directly.
                If r.Action = "import" AndAlso r.Launchable Then
                    Dim runner = EnvOr("OLYMPIUM_RUN", "/usr/libexec/dev-desktop/olympium-run")
                    If IsElf(r.FilePath) AndAlso IsExecutable(runner) Then
                        Return RunPassthrough({runner, r.FilePath})
                    End If
                    Return RunPassthrough({r.FilePath})
                End If
                ' 3. Packages & app bundles (.deb, .rpm, .dmg, .pkg, .app, .apk, .hap) ->
                '    one-step automatic native installation + automatic launch!
                If r.Action = "install" OrElse r.Action = "rpm" OrElse r.Action = "mac" OrElse
                   r.Action = "android" OrElse r.Action = "openharmony" Then
                    If Not hasGui Then
                        Dim rc = CmdImportMode(r.FilePath, Nothing, consent:=True, updateOnly:=False)
                        If rc = 0 Then
                            Dim installed = LoadRecords().LastOrDefault(Function(x) x.Path = r.FilePath OrElse x.Id = r.Id OrElse x.Launch = r.Action)
                            If installed IsNot Nothing Then Return CmdLaunch(installed.Id, Array.Empty(Of String)())
                        End If
                        Return rc
                    End If
                    Return RunAutoInstallWindow(r)
                End If
            End If

            PrintReview(r)
            If noUi OrElse Not hasGui Then Return 0
            Return RunManualDialog(r)
        End Function

        Private Function RunAutoInstallWindow(r As InstallReview) As Integer
            Dim argc = 0
            Dim argv = IntPtr.Zero
            gtk_init(argc, argv)
            Dim shownName = If(String.IsNullOrEmpty(r.MacFirst), r.Name, r.MacFirst)
            Dim win = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_title(win, $"Installing {shownName}")
            gtk_window_set_decorated(win, False)
            gtk_window_set_default_size(win, 460, 170)
            SignalConnect(win, "destroy", Sub(w, d) gtk_main_quit())

            Dim box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10)
            gtk_container_set_border_width(box, 20UI)
            gtk_container_add(win, box)

            Dim heading = gtk_label_new(Nothing)
            gtk_label_set_markup(heading, $"<span size='large' weight='bold'>Installing {MarkupEscape(shownName)}...</span>")
            gtk_label_set_xalign(heading, 0.0F)
            gtk_box_pack_start(box, heading, False, False, 0)

            Dim bar = gtk_progress_bar_new()
            gtk_progress_bar_set_fraction(bar, 0.25)
            gtk_progress_bar_set_text(bar, $"Installing {r.Detail}...")
            gtk_progress_bar_set_show_text(bar, True)
            gtk_box_pack_start(box, bar, False, False, 0)

            Dim status = gtk_label_new("Preparing application and adding to Launchpad...")
            gtk_label_set_xalign(status, 0.0F)
            gtk_label_set_line_wrap(status, True)
            gtk_box_pack_start(box, status, False, False, 0)

            Dim exitCode = 0
            Dim done = False
            Dim launchedId = ""
            Dim errMsg = ""

            Dim worker = Sub()
                             Try
                                 Dim selfBin = If(IsExecutable("/usr/bin/dev-install"), "/usr/bin/dev-install", "dev-install")
                                 Dim res = RunCmd({selfBin, "import", "--consent", r.FilePath}, 900)
                                 exitCode = res.ExitCode
                                 For Each line In res.Output.Split(vbLf)
                                     If line.StartsWith("id=") Then launchedId = line.Substring(3).Trim()
                                 Next
                                 If exitCode <> 0 Then errMsg = res.Output.Trim()
                             Catch ex As Exception
                                 exitCode = 1
                                 errMsg = ex.Message
                             Finally
                                 done = True
                             End Try
                         End Sub
            System.Threading.Tasks.Task.Run(worker)

            g_timeout_add(120UI, PinDelegate(New TimeoutCallback(Function(d)
                                                                     If Not done Then
                                                                         gtk_progress_bar_set_fraction(bar, 0.65)
                                                                         Return True
                                                                     End If
                                                                     If exitCode = 0 Then
                                                                         gtk_progress_bar_set_fraction(bar, 1.0)
                                                                         gtk_progress_bar_set_text(bar, "Installed — Launching")
                                                                         If Not String.IsNullOrEmpty(launchedId) Then
                                                                             SpawnDetached({"/usr/bin/dev-install", "launch", launchedId})
                                                                         End If
                                                                         gtk_widget_destroy(win)
                                                                     Else
                                                                         gtk_progress_bar_set_fraction(bar, 0.0)
                                                                         gtk_progress_bar_set_text(bar, "Install failed")
                                                                         gtk_label_set_text(status, If(String.IsNullOrEmpty(errMsg), $"Installation failed (exit {exitCode}).", errMsg))
                                                                         Dim closeBtn = gtk_button_new_with_label("Close")
                                                                         gtk_widget_set_halign(closeBtn, GTK_ALIGN_END)
                                                                         SignalConnect(closeBtn, "clicked", Sub(w, ud) gtk_widget_destroy(win))
                                                                         gtk_box_pack_end(box, closeBtn, False, False, 0)
                                                                         gtk_widget_show_all(win)
                                                                     End If
                                                                     Return False
                                                                 End Function)), IntPtr.Zero)
            gtk_widget_show_all(win)
            gtk_main()
            Return exitCode
        End Function

        Private Function RunManualDialog(r As InstallReview) As Integer
            Dim argc = 0
            Dim argv = IntPtr.Zero
            gtk_init(argc, argv)
            Dim win = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_title(win, "Install")
            gtk_window_set_decorated(win, False)
            gtk_window_set_default_size(win, 560, 340)
            SignalConnect(win, "destroy", Sub(w, d) gtk_main_quit())

            Dim box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10)
            gtk_container_set_border_width(box, 18UI)
            gtk_container_add(win, box)

            Dim title = gtk_label_new(Nothing)
            gtk_label_set_markup(title, $"<span size='x-large' weight='bold'>{MarkupEscape(If(String.IsNullOrEmpty(r.MacFirst), r.Name, r.MacFirst))}</span>")
            gtk_label_set_xalign(title, 0.0F)
            gtk_box_pack_start(box, title, False, False, 0)

            Dim body = gtk_label_new($"{r.Detail} · {r.Kind}{vbLf}{r.Message}")
            gtk_label_set_xalign(body, 0.0F)
            gtk_label_set_line_wrap(body, True)
            gtk_box_pack_start(box, body, False, False, 0)

            Dim canImport = (r.Action = "import" OrElse r.Action = "keep" OrElse r.Action = "install" OrElse
                             r.Action = "rpm" OrElse r.Action = "wine" OrElse r.Action = "mac" OrElse
                             r.Action = "android" OrElse r.Action = "openharmony")
            Dim chk = gtk_check_button_new_with_label(If(canImport, "Install this application and add it to Launchpad.", "This format cannot be installed here."))
            gtk_widget_set_sensitive(chk, canImport)
            gtk_box_pack_start(box, chk, False, False, 0)

            Dim row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8)
            Dim btnInstall = gtk_button_new_with_label("Install")
            Dim btnOpen = gtk_button_new_with_label("Open")
            gtk_widget_set_sensitive(btnInstall, False)
            gtk_widget_set_sensitive(btnOpen, False)
            gtk_box_pack_start(row, btnInstall, False, False, 0)
            gtk_box_pack_start(row, btnOpen, False, False, 0)
            gtk_box_pack_start(box, row, False, False, 0)

            Dim status = gtk_label_new(If(canImport, "Ready to install.", "Recognized only. Nothing was installed."))
            gtk_label_set_xalign(status, 0.0F)
            gtk_box_pack_start(box, status, False, False, 0)

            SignalConnect(chk, "toggled", Sub(w, d) gtk_widget_set_sensitive(btnInstall, canImport AndAlso gtk_toggle_button_get_active(chk)))
            SignalConnect(btnInstall, "clicked", Sub(w, d)
                                                     Dim rc = CmdImportMode(r.FilePath, Nothing, True, False)
                                                     If rc = 0 Then
                                                         gtk_label_set_text(status, "Installed. Open launches the application.")
                                                         gtk_widget_set_sensitive(btnOpen, True)
                                                     Else
                                                         gtk_label_set_text(status, $"Install failed (exit {rc}).")
                                                     End If
                                                 End Sub)
            SignalConnect(btnOpen, "clicked", Sub(w, d)
                                                  Dim rec = LoadRecords().FirstOrDefault(Function(x) x.Path = r.FilePath OrElse x.Id = r.Id)
                                                  If rec IsNot Nothing Then
                                                      SpawnDetached({"/usr/bin/dev-install", "launch", rec.Id})
                                                      gtk_label_set_text(status, "Launching...")
                                                  End If
                                              End Sub)
            gtk_widget_show_all(win)
            gtk_main()
            Return 0
        End Function

        Public Function Run(args As String()) As Integer
            If args.Length = 0 Then
                Console.Error.WriteLine("usage: dev-install inspect|import|update|rollback|launch|remove|open|prewarm|confinement|exchange ...")
                Return 2
            End If
            Select Case args(0)
                Case "inspect"
                    If args.Length < 2 Then Return 2
                    PrintReview(FillReview(args(1)))
                    Return 0
                Case "import", "update"
                    Dim consent = False
                    Dim fileArg As String = Nothing
                    Dim extArg As String = Nothing
                    Dim i = 1
                    While i < args.Length
                        If args(i) = "--consent" Then
                            consent = True
                        ElseIf args(i) = "--mime" AndAlso i + 1 < args.Length Then
                            i += 1 : extArg = args(i)
                        ElseIf fileArg Is Nothing Then
                            fileArg = args(i)
                        End If
                        i += 1
                    End While
                    If fileArg Is Nothing Then Return 2
                    Return CmdImportMode(fileArg, extArg, consent, args(0) = "update")
                Case "launch"
                    If args.Length < 2 Then Return 2
                    Dim rest = If(args.Length > 2, args.Skip(2).ToArray(), Array.Empty(Of String)())
                    Return CmdLaunch(args(1), rest)
                Case "open"
                    Dim noUi = False
                    Dim fileArg As String = Nothing
                    For i As Integer = 1 To args.Length - 1
                        If args(i) = "--no-ui" Then noUi = True Else If fileArg Is Nothing Then fileArg = args(i)
                    Next
                    If fileArg Is Nothing Then Return 2
                    Return CmdOpen(fileArg, noUi)
                Case "prewarm"
                    If args.Length < 2 Then Return 2
                    Return CmdPrewarm(args(1))
                Case "confinement"
                    Console.WriteLine(NativeBridge.ConfineName())
                    Return 0
                Case "exchange"
                    Dim dir = Path.Combine(DeskRoot(), "exchange")
                    Directory.CreateDirectory(dir)
                    Console.WriteLine($"exchange={dir}")
                    Return 0
                Case "remove"
                    If args.Length < 2 Then Return 2
                    Dim recs = LoadRecords()
                    Dim rec = recs.FirstOrDefault(Function(x) x.Id = args(1))
                    If rec Is Nothing Then Return 1
                    If rec.Launch = "android" OrElse rec.Launch = "openharmony" Then
                        Dim helper = MobileRuntime(rec.Launch = "openharmony")
                        If Not String.IsNullOrEmpty(helper) Then
                            RunPassthrough({helper, "remove", rec.Program, "--root", DeskRoot()})
                        End If
                    End If
                    recs.Remove(rec)
                    SaveRecords(recs)
                    Console.WriteLine($"removed={rec.Path}")
                    Return 0
                Case Else
                    Console.Error.WriteLine($"dev-install: unknown command {args(0)}")
                    Return 2
            End Select
        End Function

    End Module

End Namespace
