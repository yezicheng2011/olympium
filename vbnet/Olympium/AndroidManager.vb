' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Managed Android 17 + microG + scrcpy runtime controller (olympium-android) in VB.NET (.NET 8).
' Uses the offline bundled /opt/olympium/android runtime (emulator, platform-tools/adb,
' system-images/android-37.0, scrcpy, microG) with automatic fallback to system tools.

Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.IO
Imports System.IO.Compression
Imports System.Linq
Imports System.Text
Imports System.Text.Json
Imports System.Text.RegularExpressions
Imports System.Threading

Namespace Olympium

    Public Module AndroidManager
        Private ReadOnly PackageRegex As New Regex("^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z0-9_]+)+$", RegexOptions.Compiled)
        Private _runtimeDir As String = "/opt/olympium/android"
        Private _pinsPath As String = "/usr/libexec/dev-desktop/android-runtime-pins.json"

        Public Function IsValidPackage(pkg As String) As Boolean
            Return Not String.IsNullOrEmpty(pkg) AndAlso PackageRegex.IsMatch(pkg)
        End Function

        Public Function FindTool(name As String) As String
            Dim envName = "OLYMPIUM_ANDROID_" & name.ToUpperInvariant()
            Dim env = Environment.GetEnvironmentVariable(envName)
            If Not String.IsNullOrEmpty(env) AndAlso IsExecutable(env) Then Return env
            If name = "adb" Then
                Dim bundled = Path.Combine(_runtimeDir, "platform-tools", "adb")
                If IsExecutable(bundled) Then Return bundled
            ElseIf name = "scrcpy" Then
                For Each cand In {
                    Path.Combine(_runtimeDir, "scrcpy", "scrcpy"),
                    "/usr/local/bin/scrcpy",
                    "/usr/bin/scrcpy"
                }
                    If IsExecutable(cand) Then Return cand
                Next
            End If
            Dim p = FindInPath(name)
            If String.IsNullOrEmpty(p) Then
                Throw New InvalidOperationException($"Android component is not installed: {name}")
            End If
            Return p
        End Function

        Private Function Adb(serial As String, args As IList(Of String), Optional timeoutSec As Integer = 60, Optional check As Boolean = True) As String
            Dim argv As New List(Of String) From {FindTool("adb"), "-s", serial}
            argv.AddRange(args)
            Dim res = RunCmd(argv, timeoutSec)
            If res.TimedOut Then Throw New InvalidOperationException($"Android tool timed out: adb {String.Join(" ", args)}")
            If check AndAlso res.ExitCode <> 0 Then
                Dim tail = If(res.Output.Length > 1500, res.Output.Substring(res.Output.Length - 1500), res.Output).Trim()
                Throw New InvalidOperationException(If(String.IsNullOrEmpty(tail), "adb command failed", tail))
            End If
            Return res.Output
        End Function

        Public Function InspectApk(apkPath As String, rootDir As String) As Dictionary(Of String, Object)
            Dim apk = RealPath(apkPath)
            If Not File.Exists(apk) OrElse New FileInfo(apk).Length > 1024L * 1024L * 1024L Then
                Throw New InvalidOperationException("Not a supported standalone APK")
            End If
            Dim abis As New SortedSet(Of String)(StringComparer.Ordinal)
            Using zip = ZipFile.OpenRead(apk)
                If zip.GetEntry("AndroidManifest.xml") Is Nothing Then
                    Throw New InvalidOperationException("APK has no Android manifest")
                End If
                Dim abiRe As New Regex("^lib/([A-Za-z0-9_-]+)/[^/]+\.so$")
                For Each e In zip.Entries
                    Dim m = abiRe.Match(e.FullName)
                    If m.Success Then abis.Add(m.Groups(1).Value)
                Next
            End Using

            Dim aapt = FindTool("aapt")
            Dim badging = RunCmd({aapt, "dump", "badging", apk}, 60)
            If badging.ExitCode <> 0 Then Throw New InvalidOperationException("aapt failed: " & badging.Output.Trim())

            Dim pkg = ""
            Dim verCode As Long = 0
            Dim verName = ""
            Dim label = ""
            Dim minSdk = 1
            Dim bestDensity = -1
            Dim iconMember = ""
            Dim pkgRe As New Regex("^package: name='([^']+)' versionCode='([^']*)' versionName='([^']*)'")
            Dim lblRe As New Regex("^application-label:'(.*)'$")
            Dim sdkRe As New Regex("^sdkVersion:'(\d+)'$")
            Dim iconRe As New Regex("^application-icon-(\d+):'([^']+)'$")

            For Each rawLine In badging.Output.Split(vbLf)
                Dim line = rawLine.TrimEnd(vbCr(0))
                Dim m = pkgRe.Match(line)
                If String.IsNullOrEmpty(pkg) AndAlso m.Success Then
                    pkg = m.Groups(1).Value
                    Long.TryParse(m.Groups(2).Value, verCode)
                    verName = m.Groups(3).Value
                ElseIf String.IsNullOrEmpty(label) Then
                    Dim lm = lblRe.Match(line)
                    If lm.Success Then label = lm.Groups(1).Value
                End If
                Dim sm = sdkRe.Match(line)
                If sm.Success Then Integer.TryParse(sm.Groups(1).Value, minSdk)
                Dim im = iconRe.Match(line)
                If im.Success Then
                    Dim d = Integer.Parse(im.Groups(1).Value)
                    If d > bestDensity Then
                        bestDensity = d
                        iconMember = im.Groups(2).Value
                    End If
                End If
            Next

            If Not IsValidPackage(pkg) Then Throw New InvalidOperationException("Invalid Android package identity")
            Dim cleanName = If(String.IsNullOrWhiteSpace(label), pkg, label.Trim())
            Dim sha = FileDigest(apk, "sha256")
            Dim iconOut = ""
            If Not String.IsNullOrEmpty(iconMember) AndAlso iconMember.EndsWith(".png", StringComparison.OrdinalIgnoreCase) Then
                Try
                    Dim previews = Path.Combine(rootDir, "android", "previews")
                    Directory.CreateDirectory(previews)
                    Using zip = ZipFile.OpenRead(apk)
                        Dim ent = zip.GetEntry(iconMember)
                        If ent IsNot Nothing AndAlso ent.Length <= 4 * 1024 * 1024 Then
                            iconOut = Path.Combine(previews, sha & ".png")
                            ent.ExtractToFile(iconOut, True)
                        End If
                    End Using
                Catch
                    iconOut = ""
                End Try
            End If

            Return New Dictionary(Of String, Object) From {
                {"ok", True},
                {"package", pkg},
                {"name", cleanName},
                {"version", verName},
                {"version_code", verCode},
                {"id", "android-" & BytesDigestSha256(pkg).Substring(0, 16)},
                {"sha", sha},
                {"icon", iconOut},
                {"min_sdk", minSdk},
                {"native_abis", abis.ToList()},
                {"scrcpy_ready", Not String.IsNullOrEmpty(FindToolSafe("scrcpy"))},
                {"needs_runtime_download", Not File.Exists(Path.Combine(_runtimeDir, "emulator", "emulator")) AndAlso
                                           String.IsNullOrEmpty(Environment.GetEnvironmentVariable("OLYMPIUM_ANDROID_SERIAL"))}
            }
        End Function

        Private Function FindToolSafe(name As String) As String
            Try
                Return FindTool(name)
            Catch
                Return ""
            End Try
        End Function

        Private Sub StartBundledEmulator(rootDir As String)
            Dim kvmOk = (NativeBridge.access("/dev/kvm", NativeBridge.R_OK Or NativeBridge.W_OK) = 0)
            Dim accel = If(kvmOk, "on", "off")
            If Not kvmOk AndAlso EnvOr("OLYMPIUM_ANDROID_ALLOW_TCG") <> "1" Then
                Throw New InvalidOperationException("Android 17 needs hardware virtualization (/dev/kvm). Enable KVM or set OLYMPIUM_ANDROID_ALLOW_TCG=1.")
            End If
            Dim emuBin = Path.Combine(_runtimeDir, "emulator", "emulator")
            Dim sysImgDir = Path.Combine(_runtimeDir, "system-images", "android-37.0", "default", "x86_64")
            If Not IsExecutable(emuBin) OrElse Not File.Exists(Path.Combine(sysImgDir, "system.img")) Then
                Throw New InvalidOperationException($"Bundled Android 17 emulator or system image missing under {_runtimeDir}")
            End If

            Dim avdHome = Path.Combine(rootDir, "android", "avd")
            Dim avdDir = Path.Combine(avdHome, "olympium-android17.avd")
            Directory.CreateDirectory(avdDir)
            File.WriteAllText(Path.Combine(avdHome, "olympium-android17.ini"),
                $"avd.ini.encoding=UTF-8{vbLf}path={avdDir}{vbLf}target=android-37.0{vbLf}")
            File.WriteAllText(Path.Combine(avdDir, "config.ini"),
                "avd.ini.encoding=UTF-8" & vbLf &
                "abi.type=x86_64" & vbLf &
                "hw.cpu.arch=x86_64" & vbLf &
                "hw.cpu.ncore=2" & vbLf &
                "hw.ramSize=2048" & vbLf &
                "hw.lcd.width=360" & vbLf &
                "hw.lcd.height=640" & vbLf &
                "hw.lcd.density=160" & vbLf &
                "hw.gpu.enabled=yes" & vbLf &
                "hw.gpu.mode=swiftshader_indirect" & vbLf &
                "hw.keyboard=yes" & vbLf &
                $"image.sysdir.1={sysImgDir}/{vbLf}" &
                "disk.dataPartition.size=6442450944" & vbLf &
                "showDeviceFrame=no" & vbLf)

            Dim psi As New ProcessStartInfo With {
                .FileName = emuBin,
                .UseShellExecute = False
            }
            For Each a In {"-avd", "olympium-android17", "-port", "5560", "-no-window", "-no-audio",
                           "-no-snapshot", "-no-boot-anim", "-no-metrics", "-gpu", "swiftshader_indirect",
                           "-feature", "-Vulkan", "-accel", accel}
                psi.ArgumentList.Add(a)
            Next
            psi.Environment("ANDROID_AVD_HOME") = avdHome
            psi.Environment("ANDROID_SDK_ROOT") = _runtimeDir
            Process.Start(psi)

            Dim adbBin = FindTool("adb")
            Dim deadline = DateTime.UtcNow.AddSeconds(300)
            While DateTime.UtcNow < deadline
                Dim devs = RunCmd({adbBin, "devices"}, 15).Output
                If Regex.IsMatch(devs, "emulator-5560\s+device") Then Return
                Thread.Sleep(1500)
            End While
            Throw New InvalidOperationException("Android 17 emulator did not become ready before timeout.")
        End Sub

        Private Function EnsureDevice(rootDir As String) As String
            Dim serial = EnvOr("OLYMPIUM_ANDROID_SERIAL", "emulator-5560")
            Dim devs = RunCmd({FindTool("adb"), "devices"}, 15).Output
            If Not Regex.IsMatch(devs, Regex.Escape(serial) & "\s+(device|offline)") Then
                If serial <> "emulator-5560" Then
                    Throw New InvalidOperationException($"The configured Android runtime is not running: {serial}")
                End If
                StartBundledEmulator(rootDir)
            End If
            Dim deadline = DateTime.UtcNow.AddSeconds(300)
            While DateTime.UtcNow < deadline
                Try
                    If Adb(serial, {"shell", "getprop", "sys.boot_completed"}, 15).Trim() = "1" Then
                        Return serial
                    End If
                Catch
                End Try
                Thread.Sleep(2000)
            End While
            Throw New InvalidOperationException("Android did not complete boot in time.")
        End Function

        Private Sub EnsureMicroGInstalled(serial As String)
            ' Install open-source microG Services Core (GmsCore + GsfProxy, Apache-2.0) ONLY.
            ' Proprietary Google Play Store (com.android.vending.apk) is intentionally NOT bundled.
            Dim microgDir = Path.Combine(_runtimeDir, "microg")
            If Not Directory.Exists(microgDir) Then Return
            For Each apkName In {"com.google.android.gms.apk", "com.google.android.gsf.apk"}
                Dim apk = Path.Combine(microgDir, apkName)
                If Not File.Exists(apk) Then Continue For
                Dim pkg = Path.GetFileNameWithoutExtension(apkName)
                Dim present = Adb(serial, {"shell", "pm", "path", pkg}, 15, check:=False)
                If Not present.Contains("package:") Then
                    Adb(serial, {"install", "--no-streaming", apk}, 300, check:=False)
                End If
            Next
        End Sub

        Private Function DoInstall(apkPath As String, rootDir As String) As Dictionary(Of String, Object)
            Dim info = InspectApk(apkPath, rootDir)
            Dim pkg = CStr(info("package"))
            Dim serial = EnsureDevice(rootDir)
            EnsureMicroGInstalled(serial)

            Dim dest = Path.Combine(rootDir, "android", "packages", pkg)
            Directory.CreateDirectory(dest)
            Dim saved = Path.Combine(dest, CStr(info("sha")) & ".apk")
            File.Copy(apkPath, saved, True)
            Dim res = Adb(serial, {"install", "--no-streaming", "-r", saved}, 600)
            If Not res.Contains("Success") Then
                Throw New InvalidOperationException("Android package manager failed: " & res.Trim())
            End If
            info("path") = saved
            info("serial") = serial
            info("needs_runtime_download") = False
            AtomicWriteText(Path.Combine(dest, "installed.json"), JsonSerializer.Serialize(info, New JsonSerializerOptions With {.WriteIndented = True}))
            Return info
        End Function

        Private Sub DoLaunch(pkg As String, rootDir As String, title As String)
            If Not IsValidPackage(pkg) Then Throw New InvalidOperationException("Invalid Android package identifier")
            Dim serial = EnsureDevice(rootDir)
            Dim adbBin = FindTool("adb")
            Dim scrcpyBin = FindTool("scrcpy")

            Dim resolved = Adb(serial, {"shell", "cmd", "package", "resolve-activity", "--brief",
                                        "-a", "android.intent.action.MAIN",
                                        "-c", "android.intent.category.LAUNCHER", "-p", pkg}, 30)
            Dim compRe As New Regex("^" & Regex.Escape(pkg) & "/[A-Za-z0-9_.$]+$")
            Dim component = resolved.Split(vbLf).Select(Function(l) l.Trim()).FirstOrDefault(Function(l) compRe.IsMatch(l))
            If String.IsNullOrEmpty(component) Then Throw New InvalidOperationException("No unique launcher activity for " & pkg)

            ' Launch scrcpy with virtual display for this Android app and start the activity inside it.
            Dim env As New Dictionary(Of String, String) From {
                {"ADB", adbBin},
                {"SDL_APP_ID", pkg}
            }
            Dim scrcpyArgs As New List(Of String) From {
                scrcpyBin, "-s", serial,
                "--new-display=360x640/160",
                "--no-vd-system-decorations",
                "--window-title=" & If(String.IsNullOrEmpty(title), pkg, title),
                "--no-audio",
                "--max-fps=30",
                $"--start-app={pkg}"
            }
            Dim rc = RunPassthrough(scrcpyArgs, env)
            If rc <> 0 Then
                ' Fallback for scrcpy 1.x (Ubuntu apt version) which mirrors display 0 directly.
                Adb(serial, {"shell", "am", "start", "-W", "-n", component}, 60)
                rc = RunPassthrough({scrcpyBin, "-s", serial, "--window-title=" & If(String.IsNullOrEmpty(title), pkg, title), "--no-audio"}, env)
                If rc <> 0 Then Throw New InvalidOperationException($"scrcpy exited with status {rc}")
            End If
        End Sub

        Public Function Run(args As String()) As Integer
            Dim cmd = ""
            Dim val = ""
            Dim rootDir = DeskRoot()
            Dim title = ""
            Dim i = 0
            While i < args.Length
                Select Case args(i)
                    Case "--root" : i += 1 : If i < args.Length Then rootDir = args(i)
                    Case "--runtime" : i += 1 : If i < args.Length Then _runtimeDir = args(i)
                    Case "--pins" : i += 1 : If i < args.Length Then _pinsPath = args(i)
                    Case "--title" : i += 1 : If i < args.Length Then title = args(i)
                    Case Else
                        If String.IsNullOrEmpty(cmd) Then cmd = args(i) Else If String.IsNullOrEmpty(val) Then val = args(i)
                End Select
                i += 1
            End While

            Try
                Select Case cmd
                    Case "status"
                        Dim microgDir = Path.Combine(_runtimeDir, "microg")
                        Dim st = New Dictionary(Of String, Object) From {
                            {"ok", True},
                            {"runtime_dir", _runtimeDir},
                            {"emulator_present", IsExecutable(Path.Combine(_runtimeDir, "emulator", "emulator"))},
                            {"system_image_present", File.Exists(Path.Combine(_runtimeDir, "system-images", "android-37.0", "default", "x86_64", "system.img"))},
                            {"adb", FindToolSafe("adb")},
                            {"aapt", FindToolSafe("aapt")},
                            {"scrcpy", FindToolSafe("scrcpy")},
                            {"microg_core_bundled", File.Exists(Path.Combine(microgDir, "com.google.android.gms.apk"))},
                            {"google_play_store_bundled", False},
                            {"shared_clipboard_enabled", InteropHub.GetBoolSetting("clipboard_sync_enabled", True)},
                            {"widgets_bridge_enabled", InteropHub.GetBoolSetting("android_widgets_enabled", True)},
                            {"show_google_play_guide", InteropHub.GetBoolSetting("show_google_play_guide", True)}
                        }
                        Console.WriteLine(JsonSerializer.Serialize(st, New JsonSerializerOptions With {.WriteIndented = True}))
                        Return 0
                    Case "inspect"
                        Console.WriteLine(JsonSerializer.Serialize(InspectApk(val, rootDir)))
                        Return 0
                    Case "install", "update"
                        Console.WriteLine(JsonSerializer.Serialize(DoInstall(val, rootDir)))
                        Return 0
                    Case "launch"
                        DoLaunch(val, rootDir, title)
                        Return 0
                    Case "remove"
                        Dim serial = EnsureDevice(rootDir)
                        Adb(serial, {"uninstall", val}, 120, check:=False)
                        Dim pkgDir = Path.Combine(rootDir, "android", "packages", val)
                        If Directory.Exists(pkgDir) Then Directory.Delete(pkgDir, True)
                        Console.WriteLine(JsonSerializer.Serialize(New Dictionary(Of String, Object) From {{"ok", True}, {"package", val}}))
                        Return 0
                    Case Else
                        Console.Error.WriteLine("Usage: olympium-android {status|inspect|install|update|launch|remove} [VALUE] --root DIR")
                        Return 2
                End Select
            Catch ex As Exception
                Console.WriteLine(JsonSerializer.Serialize(New Dictionary(Of String, Object) From {{"ok", False}, {"message", ex.Message}}))
                Console.Error.WriteLine("Android: " & ex.Message)
                Return 3
            End Try
        End Function

    End Module

End Namespace
