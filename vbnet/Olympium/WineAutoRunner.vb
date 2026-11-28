' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Transparent automatic Wine execution engine (olympium-wine-exec) in VB.NET.
' Registered with Linux binfmt_misc for MZ/PE binaries and called directly by
' Files and olympium-run so clicking or executing any Windows .exe works just
' like a native application without "Exec format error" or manual Wine setup.

Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Text

Namespace Olympium

    Public Module WineAutoRunner

        Public Function WinePrefixPath() As String
            Dim env = Environment.GetEnvironmentVariable("OLYMPIUM_WINE_PREFIX")
            If Not String.IsNullOrEmpty(env) AndAlso env.StartsWith("/") Then Return env
            Return Path.Combine(DeskRoot(), "wine")
        End Function

        Public Function CanRunMachine(machine As UShort, ByRef why As String) As Boolean
            Dim host = UnameMachine()
            Dim hostX64 = (host = "x86_64" OrElse host = "x64")
            Dim hostX86 = (host = "i386" OrElse host = "i686" OrElse host = "x86")
            Dim hostArm64 = (host = "aarch64" OrElse host = "arm64")
            Dim what = PeMachineName(machine)

            If machine = &H8664US Then
                If hostX64 Then Return True
                If hostArm64 AndAlso (Not String.IsNullOrEmpty(FindInPath("box64")) OrElse IsExecutable("/usr/bin/qemu-x86_64-static")) Then
                    Return True
                End If
                why = If(hostArm64,
                    $"Windows program for {what}. Install box64 or qemu-x86_64-static to run x86_64 Windows programs on ARM64.",
                    $"Windows program for {what}. This system is 32-bit and cannot run it.")
                Return False
            End If
            If machine = &H14CUS Then
                If hostX86 Then Return True
                If hostX64 Then
                    Dim forced = Environment.GetEnvironmentVariable("OLYMPIUM_WINE32")
                    Dim hasWine32 = If(forced IsNot Nothing,
                        forced = "1",
                        IsExecutable("/usr/lib/wine/wine") OrElse IsExecutable("/usr/lib/i386-linux-gnu/wine/wine") OrElse IsExecutable("/usr/bin/wine32"))
                    If hasWine32 Then Return True
                    why = $"Windows program for {what}. The 32-bit half of Wine (wine32) is not installed on this image, so it was not started."
                    Return False
                End If
                If hostArm64 AndAlso (Not String.IsNullOrEmpty(FindInPath("box86")) OrElse Not String.IsNullOrEmpty(FindInPath("box64")) OrElse IsExecutable("/usr/bin/qemu-i386-static")) Then
                    Return True
                End If
                why = $"Windows program for {what}. Install box86/qemu-i386-static to run 32-bit x86 Windows programs on ARM64."
                Return False
            End If
            If machine = &HAA64US Then
                If hostArm64 Then Return True
                If hostX64 AndAlso IsExecutable("/usr/bin/qemu-aarch64-static") Then Return True
                why = $"Windows program for {what}. This computer is not ARM and qemu-aarch64-static is not available."
                Return False
            End If
            why = $"Windows program for {what}. Wine on this system cannot run it."
            Return False
        End Function

        Public Function PreparePrefix(prefix As String, ByRef elapsedMs As Long) As String
            Dim sw = Stopwatch.StartNew()
            elapsedMs = 0
            If File.Exists(Path.Combine(prefix, "system.reg")) Then
                Return "existing"
            End If
            Dim tpl = EnvOr("OLYMPIUM_WINE_TEMPLATE", "/usr/lib/olympium/wine-prefix")
            Dim prefixNonEmpty = Directory.Exists(prefix) AndAlso Directory.GetFileSystemEntries(prefix).Length > 0
            If Not File.Exists(Path.Combine(tpl, "system.reg")) OrElse prefixNonEmpty Then
                Directory.CreateDirectory(prefix)
                Return "fresh"
            End If

            Dim tmp = $"{prefix}.seed-{Environment.ProcessId}"
            Dim parent = Path.GetDirectoryName(prefix)
            If Not String.IsNullOrEmpty(parent) Then Directory.CreateDirectory(parent)
            Directory.CreateDirectory(tmp)
            Dim cp = RunCmd({"/bin/cp", "-a", "--reflink=auto", tpl & "/.", tmp}, 180)
            If cp.ExitCode = 0 Then
                Try
                    If Directory.Exists(prefix) Then Directory.Delete(prefix, False)
                    Directory.Move(tmp, prefix)
                    elapsedMs = sw.ElapsedMilliseconds
                    Return "seeded"
                Catch
                End Try
            End If
            Try : Directory.Delete(tmp, True) : Catch : End Try
            Directory.CreateDirectory(prefix)
            elapsedMs = sw.ElapsedMilliseconds
            Return "fresh"
        End Function

        Public Sub KeepWineServer(prefix As String, seconds As Integer)
            If seconds <= 0 OrElse Not IsExecutable("/usr/bin/wineserver") Then Return
            Dim env As New Dictionary(Of String, String) From {
                {"WINEPREFIX", prefix},
                {"WINEDLLOVERRIDES", "mscoree,mshtml="},
                {"WINEDEBUG", "-all"}
            }
            RunCmd({"/usr/bin/wineserver", $"-p{seconds}"}, 15, env)
        End Sub

        Public Sub EnsureExchangeDrive(prefix As String, state As String)
            If state = "fresh" Then Return
            Try
                Dim sharedDir = Path.Combine(DeskRoot(), "exchange")
                Directory.CreateDirectory(sharedDir)
                Dim dosDev = Path.Combine(prefix, "dosdevices")
                Dim driveS = Path.Combine(dosDev, "s:")
                If Directory.Exists(dosDev) AndAlso Not File.Exists(driveS) AndAlso Not Directory.Exists(driveS) Then
                    File.CreateSymbolicLink(driveS, sharedDir)
                End If
            Catch
            End Try
        End Sub

        ' Registers binfmt_misc rules for Windows PE (.exe), macOS Mach-O (Intel &
        ' M-series), and all foreign Linux ELF architectures (x86/x86_64/arm32/arm64).
        Public Sub EnsureBinfmtRegistered()
            EnsureAllBinfmtRegistered()
        End Sub

        Public Sub EnsureAllBinfmtRegistered()
            Try
                Const regPath As String = "/proc/sys/fs/binfmt_misc/register"
                If Not File.Exists(regPath) Then
                    If NativeBridge.geteuid() = 0 AndAlso Directory.Exists("/proc/sys/fs/binfmt_misc") Then
                        RunCmd({"mount", "-t", "binfmt_misc", "binfmt_misc", "/proc/sys/fs/binfmt_misc"}, 5)
                    End If
                End If
                If Not File.Exists(regPath) Then Return

                Dim regRule = Sub(entryName As String, handler As String, rule As String)
                                  Try
                                      If File.Exists("/proc/sys/fs/binfmt_misc/" & entryName) Then Return
                                      If Not IsExecutable(handler) Then Return
                                      File.WriteAllText(regPath, rule)
                                  Catch
                                  End Try
                              End Sub

                Dim wineExec = "/usr/libexec/dev-desktop/olympium-wine-exec"
                regRule("olympium-wine", wineExec, $":olympium-wine:M::MZ::{wineExec}:")

                Dim macExec = "/usr/libexec/dev-desktop/olympium-mac-run"
                regRule("olympium-macho64", macExec, $":olympium-macho64:M::\xcf\xfa\xed\xfe::{macExec}:")
                regRule("olympium-machofat", macExec, $":olympium-machofat:M::\xca\xfe\xba\xbe::{macExec}:")

                Dim host = UnameMachine()
                Dim isX64 = (host = "x86_64" OrElse host = "x64")
                Dim isX86 = (host = "i386" OrElse host = "i686")
                Dim isArm64 = (host = "aarch64" OrElse host = "arm64")
                Dim isArm32 = host.StartsWith("arm") AndAlso Not isArm64

                If Not isArm64 Then
                    regRule("olympium-aarch64", "/usr/bin/qemu-aarch64-static",
                            ":olympium-aarch64:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\xb7\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-aarch64-static:OCF")
                End If
                If Not isArm32 AndAlso Not isArm64 Then
                    regRule("olympium-arm", "/usr/bin/qemu-arm-static",
                            ":olympium-arm:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-arm-static:OCF")
                End If
                If Not isX64 Then
                    regRule("olympium-x86_64", "/usr/bin/qemu-x86_64-static",
                            ":olympium-x86_64:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x3e\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-x86_64-static:OCF")
                End If
                If Not isX86 AndAlso Not isX64 Then
                    regRule("olympium-i386", "/usr/bin/qemu-i386-static",
                            ":olympium-i386:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x03\x00:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/usr/bin/qemu-i386-static:OCF")
                End If
            Catch
            End Try
        End Sub

        ' Scans Wine's Program Files for newly installed notable apps (e.g. Chrome)
        ' and ensures a Launchpad .desktop launcher exists pointing to olympium-wine-exec.
        Public Sub SyncInstalledShortcuts(prefix As String)
            Try
                Dim wineExec = "/usr/libexec/dev-desktop/olympium-wine-exec"
                If Not IsExecutable(wineExec) Then Return
                Dim apps = AppsDir()
                Directory.CreateDirectory(apps)
                Dim knownTargets As (Name As String, RelPath As String)() = {
                    ("Google Chrome (Windows)", "drive_c/Program Files/Google/Chrome/Application/chrome.exe"),
                    ("Google Chrome (Windows)", "drive_c/Program Files (x86)/Google/Chrome/Application/chrome.exe")
                }
                For Each item In knownTargets
                    Dim exe = Path.Combine(prefix, item.RelPath)
                    If File.Exists(exe) Then
                        Dim id = "wine-auto-" & BytesDigestSha256(exe).Substring(0, 8)
                        Dim desktopFile = Path.Combine(apps, $"olympium-{id}.desktop")
                        Dim content =
                            "[Desktop Entry]" & vbLf &
                            "Type=Application" & vbLf &
                            $"Name={item.Name}" & vbLf &
                            $"Exec={wineExec} {ShQuote(exe)}" & vbLf &
                            "Icon=olympium" & vbLf &
                            "Terminal=false" & vbLf &
                            "Categories=Network;WebBrowser;Utility;" & vbLf
                        AtomicWriteText(desktopFile, content)
                    End If
                Next
            Catch
            End Try
        End Sub

        Public Function LaunchExe(exePath As String, extraArgs As String(), Optional emitMarks As Boolean = False) As Integer
            If String.IsNullOrEmpty(exePath) OrElse Not File.Exists(exePath) Then
                Console.Error.WriteLine($"olympium-wine-exec: file not found: {exePath}")
                Return 1
            End If
            Dim wineBin = If(IsExecutable("/usr/bin/wine"), "/usr/bin/wine", FindInPath("wine"))
            If String.IsNullOrEmpty(wineBin) Then
                Console.Error.WriteLine("olympium-wine-exec: Wine is not installed on this system.")
                Return 3
            End If

            Dim machine = ReadPeMachine(exePath)
            If machine <> 0 Then
                Dim why = ""
                If Not CanRunMachine(machine, why) Then
                    Console.Error.WriteLine($"olympium-wine-exec: {why}")
                    Return 3
                End If
            End If

            Dim xdisplay = FindXDisplay()
            If String.IsNullOrEmpty(xdisplay) Then
                Console.Error.WriteLine($"olympium-wine-exec: {exePath} was not started. No Xwayland display is running.")
                Return 3
            End If

            Dim prefix = WinePrefixPath()
            Dim t0 = Environment.TickCount64
            Dim prefixMs As Long = 0
            Dim state = PreparePrefix(prefix, prefixMs)
            EnsureExchangeDrive(prefix, state)

            Dim idleSec = 60
            Dim idleEnv = Environment.GetEnvironmentVariable("OLYMPIUM_WINE_IDLE")
            If Not String.IsNullOrEmpty(idleEnv) Then
                Dim parsed As Integer
                If Integer.TryParse(idleEnv, parsed) Then idleSec = Math.Clamp(parsed, 0, 86400)
            End If
            Dim t1 = Environment.TickCount64
            KeepWineServer(prefix, idleSec)
            Dim t2 = Environment.TickCount64

            If emitMarks Then
                Console.WriteLine("engine wine")
                Console.WriteLine($"wine_prefix {state}")
                Console.WriteLine($"wine_prefix_ms {prefixMs}")
                Console.WriteLine($"wine_server_ms {t2 - t1}")
                Console.WriteLine($"launch_start_ms {t0}")
                Console.Out.Flush()
                Try
                    Dim markPath = Path.Combine(DeskRoot(), "wine-launch-marks")
                    Directory.CreateDirectory(DeskRoot())
                    File.WriteAllText(markPath,
                        $"launch_start_ms {t0}" & vbLf &
                        $"wine_prefix {state}" & vbLf &
                        $"wine_prefix_ms {prefixMs}" & vbLf &
                        $"wine_server_ms {t2 - t1}" & vbLf)
                Catch
                End Try
            End If

            Dim env As New Dictionary(Of String, String) From {
                {"DISPLAY", xdisplay},
                {"WINEPREFIX", prefix},
                {"WINEDLLOVERRIDES", "mscoree,mshtml="},
                {"WINEDEBUG", EnvOr("WINEDEBUG", "-all")}
            }
            Dim argv As New List(Of String)()
            Dim host = UnameMachine()
            If (host = "aarch64" OrElse host = "arm64") AndAlso (machine = &H8664US OrElse machine = &H14CUS) Then
                Dim box64 = FindInPath("box64")
                If Not String.IsNullOrEmpty(box64) Then
                    argv.Add(box64)
                ElseIf IsExecutable("/usr/bin/qemu-x86_64-static") Then
                    argv.Add("/usr/bin/qemu-x86_64-static")
                End If
            End If
            argv.Add(wineBin)
            If exePath.EndsWith(".msi", StringComparison.OrdinalIgnoreCase) Then
                argv.Add("msiexec")
                argv.Add("/i")
            End If
            argv.Add(exePath)
            If extraArgs IsNot Nothing Then
                For Each a In extraArgs
                    If a.StartsWith("/") AndAlso File.Exists(a) Then
                        argv.Add("Z:" & a.Replace("/"c, "\"c))
                    Else
                        argv.Add(a)
                    End If
                Next
            End If

            Dim rc = RunPassthrough(argv, env)
            SyncInstalledShortcuts(prefix)
            Return rc
        End Function

        Public Function Run(args As String()) As Integer
            If args.Length >= 1 AndAlso args(0) = "--register-binfmt" Then
                EnsureBinfmtRegistered()
                Return 0
            End If
            If args.Length = 0 Then
                Console.Error.WriteLine("Usage: olympium-wine-exec PROGRAM.exe [ARG...]")
                Return 2
            End If
            Dim rest As String() = If(args.Length > 1, args.Skip(1).ToArray(), Array.Empty(Of String)())
            Return LaunchExe(args(0), rest, emitMarks:=False)
        End Function

    End Module

End Namespace
