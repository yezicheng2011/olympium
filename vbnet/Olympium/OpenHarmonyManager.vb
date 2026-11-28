' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' OpenHarmony HAP manager (olympium-openharmony) in VB.NET (.NET 8).
' Automatically discovers or configures & boots a minimal local OpenHarmony SDK
' session (hdc + headless QEMU + QMP bridge + private mode-0600 session.json)
' so users never need manual SDK setup.

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

    Public Module OpenHarmonyManager
        Private ReadOnly BundleRe As New Regex("^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$", RegexOptions.Compiled)
        Private ReadOnly IdentRe As New Regex("^[A-Za-z_][A-Za-z0-9_.]*$", RegexOptions.Compiled)
        Private ReadOnly TargetRe As New Regex("^127\.0\.0\.1:([1-9][0-9]{0,4})$", RegexOptions.Compiled)
        Private Const DefaultRuntimeDir As String = "/opt/olympium/openharmony"

        Public Function IsValidBundle(s As String) As Boolean
            Return Not String.IsNullOrEmpty(s) AndAlso s.Length <= 128 AndAlso BundleRe.IsMatch(s)
        End Function

        Public Function IsValidIdentifier(s As String) As Boolean
            Return Not String.IsNullOrEmpty(s) AndAlso s.Length <= 128 AndAlso IdentRe.IsMatch(s)
        End Function

        Private Function SanitizeLabel(raw As String, fallback As String) As String
            If String.IsNullOrWhiteSpace(raw) Then Return fallback
            Dim words = raw.Split({" "c, vbTab(0), vbCr(0), vbLf(0)}, StringSplitOptions.RemoveEmptyEntries)
            Dim joined = String.Join(" ", words)
            Dim sb As New StringBuilder()
            For Each ch In joined
                Dim code = AscW(ch)
                If code >= &H20 AndAlso code <> &H7F Then sb.Append(ch)
            Next
            Dim res = sb.ToString()
            If res.Length > 120 Then res = res.Substring(0, 120)
            Return If(String.IsNullOrEmpty(res), fallback, res)
        End Function

        Public Function PackageInfo(hapPath As String, rootDir As String) As Dictionary(Of String, Object)
            If Not File.Exists(hapPath) Then Throw New InvalidOperationException("Expected a regular HAP file.")
            Dim bundle = ""
            Dim ability = ""
            Dim label = ""
            Dim iconRef = ""
            Dim iconPath = ""
            Dim sha = FileDigest(hapPath, "sha256")

            Using zip = ZipFile.OpenRead(hapPath)
                Dim modEnt = zip.GetEntry("module.json")
                If modEnt Is Nothing Then Throw New InvalidOperationException("HAP is missing module.json.")
                If modEnt.Length > 512 * 1024 Then Throw New InvalidOperationException("HAP module.json exceeds limit.")
                Dim jsonText As String
                Using sr As New StreamReader(modEnt.Open(), Encoding.UTF8)
                    jsonText = sr.ReadToEnd()
                End Using
                Using doc = JsonDocument.Parse(jsonText)
                    Dim root = doc.RootElement
                    Dim app = root.GetProperty("app")
                    Dim [mod] = root.GetProperty("module")
                    bundle = app.GetProperty("bundleName").GetString()
                    ability = [mod].GetProperty("mainElement").GetString()
                    Dim modType = If([mod].TryGetProperty("type", Nothing), [mod].GetProperty("type").GetString(), "")
                    Dim exportedMain = False
                    Dim abilEl As JsonElement
                    If [mod].TryGetProperty("abilities", abilEl) AndAlso abilEl.ValueKind = JsonValueKind.Array Then
                        For Each a In abilEl.EnumerateArray()
                            Dim nm = If(a.TryGetProperty("name", Nothing), a.GetProperty("name").GetString(), "")
                            Dim exp = a.TryGetProperty("exported", Nothing) AndAlso a.GetProperty("exported").ValueKind = JsonValueKind.True
                            If nm = ability AndAlso exp Then
                                exportedMain = True
                                Exit For
                            End If
                        Next
                    End If
                    If Not IsValidBundle(bundle) OrElse Not IsValidIdentifier(ability) OrElse modType <> "entry" OrElse Not exportedMain Then
                        Throw New InvalidOperationException("Expected an entry HAP with an exported main ability.")
                    End If
                    If app.TryGetProperty("label", Nothing) Then label = app.GetProperty("label").GetString()
                    If app.TryGetProperty("icon", Nothing) Then iconRef = app.GetProperty("icon").GetString()
                End Using

                If String.IsNullOrEmpty(label) OrElse label.StartsWith("$") Then
                    Dim stem = Path.GetFileNameWithoutExtension(hapPath)
                    label = If(Not String.IsNullOrEmpty(stem) AndAlso stem <> bundle, stem, bundle)
                End If
                label = SanitizeLabel(label, bundle)
                Dim identifier = "oh-" & BytesDigestSha256(bundle).Substring(0, 16)

                Dim mediaMatch = Regex.Match(If(iconRef, ""), "^\$media:([A-Za-z0-9_]+)$")
                If mediaMatch.Success Then
                    Dim memberName = $"resources/base/media/{mediaMatch.Groups(1).Value}.png"
                    Dim ient = zip.GetEntry(memberName)
                    If ient IsNot Nothing AndAlso ient.Length <= 1024 * 1024 Then
                        Using ms As New MemoryStream()
                            ient.Open().CopyTo(ms)
                            Dim data = ms.ToArray()
                            If data.Length >= 24 AndAlso data(0) = &H89 AndAlso data(1) = AscW("P"c) AndAlso
                               data(2) = AscW("N"c) AndAlso data(3) = AscW("G"c) Then
                                Dim iconsDir = Path.Combine(rootDir, "icons")
                                Directory.CreateDirectory(iconsDir)
                                NativeBridge.chmod(iconsDir, &H1C0UI) ' 0700
                                iconPath = Path.Combine(iconsDir, $"{identifier}-{sha.Substring(0, 16)}.png")
                                File.WriteAllBytes(iconPath, data)
                            End If
                        End Using
                    End If
                End If

                Return New Dictionary(Of String, Object) From {
                    {"ok", True},
                    {"id", identifier},
                    {"package", bundle},
                    {"ability", ability},
                    {"name", label},
                    {"path", hapPath},
                    {"sha", sha},
                    {"icon", iconPath}
                }
            End Using
        End Function

        Private Class OhSession
            Public ConfigPath As String
            Public HdcPath As String
            Public QmpPath As String
            Public Target As String

            Public Function Command(words As IList(Of String), Optional timeoutSec As Integer = 30, Optional withTarget As Boolean = True) As String
                Dim argv As New List(Of String) From {HdcPath}
                If withTarget Then
                    argv.Add("-t") : argv.Add(Target)
                End If
                argv.AddRange(words)
                Dim res = RunCmd(argv, timeoutSec)
                If res.ExitCode <> 0 Then
                    Dim msg = res.Output.Trim()
                    If msg.Length > 240 Then msg = msg.Substring(0, 240)
                    Throw New InvalidOperationException("SDK command failed: " & msg)
                End If
                Return res.Output
            End Function

            Public Function Shell(words As IList(Of String), Optional timeoutSec As Integer = 30) As String
                Dim argv As New List(Of String) From {"shell"}
                argv.AddRange(words)
                Return Command(argv, timeoutSec, withTarget:=True)
            End Function

            Public Sub Connect()
                Command({"tconn", Target}, 15, withTarget:=False)
                Dim targets = Command({"list", "targets"}, 15, withTarget:=False).Split({" "c, vbTab(0), vbCr(0), vbLf(0)}, StringSplitOptions.RemoveEmptyEntries)
                If Not targets.Contains(Target) Then
                    Throw New InvalidOperationException("The OpenHarmony SDK guest is not connected.")
                End If
            End Sub

            Public Function FindBounds(bundle As String) As Integer()
                Dim remote = $"/data/local/tmp/olympium-{RandomHex(8)}.json"
                Dim localDir = Path.Combine(Path.GetTempPath(), "olympium-oh-layout-" & RandomHex(6))
                Directory.CreateDirectory(localDir)
                Dim localFile = Path.Combine(localDir, "layout.json")
                Try
                    Shell({"uitest", "dumpLayout", "-p", remote}, 30)
                    Command({"file", "recv", remote, localFile}, 30)
                    Using doc = JsonDocument.Parse(File.ReadAllText(localFile))
                        Dim stack As New Stack(Of JsonElement)()
                        stack.Push(doc.RootElement)
                        While stack.Count > 0
                            Dim node = stack.Pop()
                            Dim attrs As JsonElement
                            If node.TryGetProperty("attributes", attrs) AndAlso attrs.ValueKind = JsonValueKind.Object Then
                                Dim bn = If(attrs.TryGetProperty("bundleName", Nothing), attrs.GetProperty("bundleName").GetString(), "")
                                Dim tp = If(attrs.TryGetProperty("type", Nothing), attrs.GetProperty("type").GetString(), "")
                                Dim vis = If(attrs.TryGetProperty("visible", Nothing), attrs.GetProperty("visible").GetString(), "")
                                If bn = bundle AndAlso tp = "root" AndAlso vis = "true" Then
                                    Dim bs = If(attrs.TryGetProperty("bounds", Nothing), attrs.GetProperty("bounds").GetString(), "")
                                    Dim matches = Regex.Matches(bs, "-?\d+")
                                    If matches.Count = 4 Then
                                        Dim c = matches.Select(Function(m) Integer.Parse(m.Value)).ToArray()
                                        If 0 <= c(0) AndAlso c(0) < c(2) AndAlso c(2) <= 360 AndAlso
                                           0 <= c(1) AndAlso c(1) < c(3) AndAlso c(3) <= 720 Then
                                            Return c
                                        End If
                                    End If
                                End If
                            End If
                            Dim children As JsonElement
                            If node.TryGetProperty("children", children) AndAlso children.ValueKind = JsonValueKind.Array Then
                                Dim arr = children.EnumerateArray().ToList()
                                For i As Integer = arr.Count - 1 To 0 Step -1
                                    stack.Push(arr(i))
                                Next
                            End If
                        End While
                    End Using
                Finally
                    Try : Shell({"rm", "-f", remote}, 5) : Catch : End Try
                    Try : Directory.Delete(localDir, True) : Catch : End Try
                End Try
                Return Nothing
            End Function
        End Class

        Public Function FindHdc(runtimeDir As String) As String
            For Each cand In {
                Environment.GetEnvironmentVariable("OLYMPIUM_OPENHARMONY_HDC"),
                Path.Combine(runtimeDir, "sdk", "toolchains", "hdc"),
                Path.Combine(runtimeDir, "toolchains", "hdc"),
                Path.Combine(runtimeDir, "hdc"),
                "/usr/local/bin/hdc",
                "/usr/bin/hdc",
                FindInPath("hdc")
            }
                If Not String.IsNullOrEmpty(cand) AndAlso IsExecutable(cand) Then Return cand
            Next
            Return ""
        End Function

        Private Function SessionConfigPath() As String
            Dim envCfg = Environment.GetEnvironmentVariable("OLYMPIUM_OPENHARMONY_SESSION")
            If Not String.IsNullOrEmpty(envCfg) Then Return envCfg
            Dim xdg = EnvOr("XDG_RUNTIME_DIR", $"/run/user/{NativeBridge.getuid()}")
            Return Path.Combine(xdg, "olympium-openharmony", "session.json")
        End Function

        Private Function TryLoadSession(cfgPath As String) As OhSession
            If Not File.Exists(cfgPath) Then Return Nothing
            Using doc = JsonDocument.Parse(File.ReadAllText(cfgPath))
                Dim root = doc.RootElement
                Dim hdc = root.GetProperty("hdc").GetString()
                Dim qmp = root.GetProperty("qmp").GetString()
                Dim target = root.GetProperty("target").GetString()
                Dim tm = TargetRe.Match(target)
                If Not Path.IsPathRooted(hdc) OrElse Not IsExecutable(hdc) OrElse
                   Not Path.IsPathRooted(qmp) OrElse Not File.Exists(qmp) OrElse
                   Not tm.Success OrElse Integer.Parse(tm.Groups(1).Value) > 65535 Then
                    Return Nothing
                End If
                Return New OhSession With {
                    .ConfigPath = RealPath(cfgPath),
                    .HdcPath = hdc,
                    .QmpPath = qmp,
                    .Target = target
                }
            End Using
        End Function

        ' Automatically configures & boots a minimal local OpenHarmony SDK session
        ' when no session.json exists yet, eliminating manual SDK setup.
        Private Function AutoConfigureAndStartSession(rootDir As String) As OhSession
            Dim runtimeDir = EnvOr("OLYMPIUM_OPENHARMONY_ROOT", DefaultRuntimeDir)
            Dim hdc = FindHdc(runtimeDir)
            If String.IsNullOrEmpty(hdc) Then
                Throw New InvalidOperationException(
                    $"OpenHarmony local SDK tool 'hdc' is not installed under {runtimeDir}. Run olympium-openharmony setup or install the slim OpenHarmony runtime.")
            End If

            Dim oniroDir = Path.Combine(runtimeDir, "oniro")
            Dim kernel = Path.Combine(oniroDir, "bzImage")
            Dim systemImg = Path.Combine(oniroDir, "system.img")
            Dim vendorImg = Path.Combine(oniroDir, "vendor.img")
            Dim updaterImg = Path.Combine(oniroDir, "updater.img")
            Dim ramdisk = Path.Combine(oniroDir, "ramdisk.img")
            Dim baseUserdata = Path.Combine(oniroDir, "userdata.img")

            If Not File.Exists(kernel) OrElse Not File.Exists(systemImg) Then
                Throw New InvalidOperationException(
                    $"Minimal OpenHarmony runtime images are not staged under {oniroDir}. Run image/stage-mobile-slim.sh to stage the pruned OpenHarmony runtime.")
            End If

            Dim qemu = If(IsExecutable("/usr/bin/qemu-system-x86_64"), "/usr/bin/qemu-system-x86_64", FindInPath("qemu-system-x86_64"))
            If String.IsNullOrEmpty(qemu) Then
                Throw New InvalidOperationException("qemu-system-x86_64 is required to start the local OpenHarmony session.")
            End If

            Dim cfgPath = SessionConfigPath()
            Dim sessionDir = Path.GetDirectoryName(cfgPath)
            Directory.CreateDirectory(sessionDir)
            NativeBridge.chmod(sessionDir, &H1C0UI) ' 0700

            Dim stateDir = Path.Combine(rootDir, "openharmony", "session")
            Directory.CreateDirectory(stateDir)
            NativeBridge.chmod(stateDir, &H1C0UI)

            Dim userDataCopy = Path.Combine(stateDir, "userdata.img")
            If Not File.Exists(userDataCopy) AndAlso File.Exists(baseUserdata) Then
                File.Copy(baseUserdata, userDataCopy, True)
            End If

            Dim qmpSock = Path.Combine(sessionDir, "qmp.sock")
            If File.Exists(qmpSock) Then File.Delete(qmpSock)
            Const hdcPort As Integer = 55555
            Dim target = $"127.0.0.1:{hdcPort}"

            Dim kvmOk = (NativeBridge.access("/dev/kvm", NativeBridge.R_OK Or NativeBridge.W_OK) = 0)
            Dim argv As New List(Of String) From {
                qemu,
                "-machine", If(kvmOk, "pc,accel=kvm", "pc,accel=tcg"),
                "-m", "2048", "-smp", "2",
                "-display", "none",
                "-vga", "std",
                "-qmp", $"unix:{qmpSock},server=on,wait=off",
                "-netdev", $"user,id=net0,hostfwd=tcp:127.0.0.1:{hdcPort}-:55555",
                "-device", "virtio-net-pci,netdev=net0",
                "-kernel", kernel,
                "-append", "ip=dhcp loglevel=3 console=ttyS0,115200 init=init root=/dev/ram0 rw ohos.boot.hardware=virt default_boot_device=soc/10007000.virtio_mmio sn=8823456789 ohos.required_mount.system=/dev/block/vda@/usr@ext4@ro,barrier=1@wait,required ohos.required_mount.vendor=/dev/block/vdb@/vendor@ext4@ro,barrier=1@wait,required"
            }
            If File.Exists(ramdisk) Then
                argv.Add("-initrd") : argv.Add(ramdisk)
            End If
            argv.AddRange({"-drive", $"file={systemImg},if=virtio,format=raw,readonly=on,snapshot=on"})
            If File.Exists(vendorImg) Then argv.AddRange({"-drive", $"file={vendorImg},if=virtio,format=raw,readonly=on,snapshot=on"})
            If File.Exists(updaterImg) Then argv.AddRange({"-drive", $"file={updaterImg},if=virtio,format=raw,readonly=on,snapshot=on"})
            If File.Exists(userDataCopy) Then argv.AddRange({"-drive", $"file={userDataCopy},if=virtio,format=raw"})

            Dim psi As New ProcessStartInfo With {
                .FileName = argv(0),
                .UseShellExecute = False
            }
            For i As Integer = 1 To argv.Count - 1
                psi.ArgumentList.Add(argv(i))
            Next
            Process.Start(psi)

            ' Wait for QMP socket and HDC readiness
            Dim deadline = DateTime.UtcNow.AddSeconds(180)
            While DateTime.UtcNow < deadline
                If File.Exists(qmpSock) Then
                    Dim conn = RunCmd({hdc, "tconn", target}, 10)
                    If conn.ExitCode = 0 Then
                        Dim listed = RunCmd({hdc, "list", "targets"}, 10).Output
                        If listed.Contains(target) Then Exit While
                    End If
                End If
                Thread.Sleep(2000)
            End While
            If Not File.Exists(qmpSock) Then
                Throw New InvalidOperationException("Local OpenHarmony emulator failed to create its QMP socket.")
            End If

            Dim desc = New Dictionary(Of String, Object) From {
                {"hdc", hdc},
                {"qmp", qmpSock},
                {"target", target},
                {"auto_configured", True}
            }
            AtomicWriteText(cfgPath, JsonSerializer.Serialize(desc, New JsonSerializerOptions With {.WriteIndented = True}) & vbLf, &H180UI) ' 0600
            Return New OhSession With {
                .ConfigPath = RealPath(cfgPath),
                .HdcPath = hdc,
                .QmpPath = qmpSock,
                .Target = target
            }
        End Function

        Private Function EnsureSession(rootDir As String, allowAutoStart As Boolean) As OhSession
            Dim cfg = SessionConfigPath()
            Dim existing = TryLoadSession(cfg)
            If existing IsNot Nothing Then Return existing
            If Not allowAutoStart Then
                Throw New InvalidOperationException("Start and configure a local OpenHarmony SDK session first.")
            End If
            Return AutoConfigureAndStartSession(rootDir)
        End Function

        Private Function FindWindowHelper() As String
            For Each cand In {
                "/usr/libexec/dev-desktop/openharmony-window",
                Path.Combine(AppContext.BaseDirectory, "openharmony-window"),
                Path.Combine(Directory.GetCurrentDirectory(), "image", "openharmony-window")
            }
                If IsExecutable(cand) Then Return cand
            Next
            Throw New InvalidOperationException("openharmony-window binary is not installed.")
        End Function

        Private Function DoInstall(session As OhSession, source As String, rootDir As String) As Dictionary(Of String, Object)
            If Not File.Exists(source) Then Throw New InvalidOperationException("Expected a regular HAP file.")
            Dim ohDir = Path.Combine(rootDir, "openharmony")
            Directory.CreateDirectory(ohDir)
            NativeBridge.chmod(ohDir, &H1C0UI)

            Dim info = PackageInfo(source, rootDir)
            Dim bundle = CStr(info("package"))
            Dim saved = Path.Combine(ohDir, bundle & ".hap")
            If File.Exists(saved) AndAlso FileDigest(saved, "sha256") <> CStr(info("sha")) Then
                Throw New InvalidOperationException("This bundle is already in the library with different contents.")
            End If

            Dim remote = $"/data/local/tmp/olympium-{RandomHex(8)}.hap"
            Try
                session.Command({"file", "send", source, remote}, 120)
                Dim res = session.Shell({"bm", "install", "-p", remote}, 120)
                If Not res.ToLowerInvariant().Contains("install bundle successfully") Then
                    Throw New InvalidOperationException("Bundle Manager rejected the HAP: " & res.Trim())
                End If
            Finally
                Try : session.Shell({"rm", "-f", remote}, 5) : Catch : End Try
            End Try

            File.Copy(source, saved, True)
            info("path") = saved
            Return info
        End Function

        Private Sub DoLaunch(session As OhSession, bundle As String, rootDir As String, title As String)
            Dim saved = Path.Combine(rootDir, "openharmony", bundle & ".hap")
            Dim info = PackageInfo(saved, rootDir)
            Dim res = session.Shell({"aa", "start", "-a", CStr(info("ability")), "-b", bundle}, 30)
            If Not res.ToLowerInvariant().Contains("start ability successfully") Then
                Throw New InvalidOperationException("Ability Manager could not open the application: " & res.Trim())
            End If
            Try
                Dim deadline = DateTime.UtcNow.AddSeconds(60)
                Dim bounds As Integer() = Nothing
                While DateTime.UtcNow < deadline
                    Try : bounds = session.FindBounds(bundle) : Catch : bounds = Nothing : End Try
                    If bounds IsNot Nothing Then Exit While
                    Thread.Sleep(1000)
                End While
                If bounds Is Nothing Then Throw New InvalidOperationException("The guest did not show an application-owned window.")
                Dim winBin = FindWindowHelper()
                Dim rc = RunPassthrough({
                    winBin,
                    "--qmp", session.QmpPath,
                    "--hdc", session.HdcPath,
                    "--target", session.Target,
                    "--title", If(String.IsNullOrEmpty(title), CStr(info("name")), title),
                    "--bounds", bounds(0).ToString(), bounds(1).ToString(), bounds(2).ToString(), bounds(3).ToString()
                })
                If rc <> 0 Then Throw New InvalidOperationException($"OpenHarmony window exited with status {rc}")
            Finally
                Try : session.Shell({"aa", "force-stop", bundle}, 5) : Catch : End Try
                Console.WriteLine("OpenHarmony app stopped")
                Console.Out.Flush()
            End Try
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
                    Case "--title" : i += 1 : If i < args.Length Then title = args(i)
                    Case Else
                        If String.IsNullOrEmpty(cmd) Then cmd = args(i) Else If String.IsNullOrEmpty(val) Then val = args(i)
                End Select
                i += 1
            End While

            Try
                If cmd = "status" Then
                    Dim runtimeDir = EnvOr("OLYMPIUM_OPENHARMONY_ROOT", DefaultRuntimeDir)
                    Dim cfg = SessionConfigPath()
                    Dim st = New Dictionary(Of String, Object) From {
                        {"ok", True},
                        {"runtime_dir", runtimeDir},
                        {"hdc", FindHdc(runtimeDir)},
                        {"session_config", cfg},
                        {"session_active", TryLoadSession(cfg) IsNot Nothing},
                        {"minimal_images_ready", File.Exists(Path.Combine(runtimeDir, "oniro", "bzImage")) AndAlso
                                                 File.Exists(Path.Combine(runtimeDir, "oniro", "system.img"))}
                    }
                    Console.WriteLine(JsonSerializer.Serialize(st, New JsonSerializerOptions With {.WriteIndented = True}))
                    Return 0
                End If
                If cmd = "inspect" Then
                    ' Inspect never requires a running emulator or manual SDK session!
                    Console.WriteLine(JsonSerializer.Serialize(PackageInfo(val, rootDir)))
                    Return 0
                End If
                If (cmd = "launch" OrElse cmd = "remove") AndAlso Not IsValidBundle(val) Then
                    Throw New InvalidOperationException("Invalid OpenHarmony bundle name.")
                End If
                Dim allowAuto = String.IsNullOrEmpty(Environment.GetEnvironmentVariable("OLYMPIUM_OPENHARMONY_NO_AUTO"))
                Dim session = EnsureSession(rootDir, allowAuto)
                session.Connect()
                Select Case cmd
                    Case "install"
                        Console.WriteLine(JsonSerializer.Serialize(DoInstall(session, val, rootDir)))
                    Case "launch"
                        DoLaunch(session, val, rootDir, title)
                    Case "remove"
                        Dim res = session.Shell({"bm", "uninstall", "-n", val}, 60)
                        If Not res.ToLowerInvariant().Contains("uninstall bundle successfully") Then
                            Throw New InvalidOperationException("Bundle Manager could not remove the application: " & res.Trim())
                        End If
                        Dim saved = Path.Combine(rootDir, "openharmony", val & ".hap")
                        If File.Exists(saved) Then File.Delete(saved)
                        Console.WriteLine(JsonSerializer.Serialize(New Dictionary(Of String, Object) From {{"ok", True}, {"package", val}}))
                    Case Else
                        Console.Error.WriteLine("Usage: olympium-openharmony {status|inspect|install|launch|remove} VALUE --root DIR [--title TITLE]")
                        Return 2
                End Select
                Return 0
            Catch ex As Exception
                Console.WriteLine(JsonSerializer.Serialize(New Dictionary(Of String, Object) From {{"ok", False}, {"message", ex.Message}}))
                Console.Error.WriteLine("OpenHarmony: " & ex.Message)
                Return 3
            End Try
        End Function

    End Module

End Namespace
