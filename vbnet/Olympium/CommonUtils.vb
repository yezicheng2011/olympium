' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Shared utilities for Olympium VB.NET tools and desktop apps: process running,
' hashing, atomic JSON/file helpers, path discovery, and PE/ELF inspection.

Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Security.Cryptography
Imports System.Text
Imports System.Text.Json
Imports System.Threading

Namespace Olympium

    Public Structure CmdResult
        Public ExitCode As Integer
        Public Output As String
        Public TimedOut As Boolean
    End Structure

    Public Module CommonUtils

        Public Function EnvOr(name As String, Optional fallback As String = "") As String
            Dim v = Environment.GetEnvironmentVariable(name)
            Return If(String.IsNullOrEmpty(v), fallback, v)
        End Function

        Public Function DeskRoot() As String
            Dim env = Environment.GetEnvironmentVariable("OLYMPIUM_DESK_HOME")
            If Not String.IsNullOrEmpty(env) Then Return env
            Dim home = EnvOr("HOME", "/tmp")
            Return Path.Combine(home, ".local", "share", "olympium-desk")
        End Function

        Public Function AppsDir() As String
            Dim env = Environment.GetEnvironmentVariable("OLYMPIUM_DESK_HOME")
            If Not String.IsNullOrEmpty(env) Then Return Path.Combine(env, "applications")
            Dim home = EnvOr("HOME", "/tmp")
            Return Path.Combine(home, ".local", "share", "applications")
        End Function

        Public Function IsExecutable(path As String) As Boolean
            If String.IsNullOrEmpty(path) Then Return False
            Return NativeBridge.access(path, NativeBridge.X_OK) = 0
        End Function

        Public Function FindInPath(name As String) As String
            If String.IsNullOrEmpty(name) Then Return ""
            If name.Contains("/"c) Then
                Return If(IsExecutable(name), name, "")
            End If
            Dim pathEnv = EnvOr("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin")
            For Each dirPath In pathEnv.Split(":"c, StringSplitOptions.RemoveEmptyEntries)
                Dim cand = Path.Combine(dirPath, name)
                If IsExecutable(cand) Then Return cand
            Next
            Return ""
        End Function

        Public Function ShQuote(s As String) As String
            If s Is Nothing Then Return "''"
            Return "'" & s.Replace("'", "'""'""'") & "'"
        End Function

        Public Function RunCmd(argv As IList(Of String), Optional timeoutSec As Integer = 60,
                               Optional extraEnv As IDictionary(Of String, String) = Nothing,
                               Optional workDir As String = Nothing,
                               Optional monitor As Action = Nothing) As CmdResult
            Dim res As New CmdResult With {.ExitCode = -1, .Output = "", .TimedOut = False}
            If argv Is Nothing OrElse argv.Count = 0 Then Return res
            Dim psi As New ProcessStartInfo With {
                .FileName = argv(0),
                .UseShellExecute = False,
                .RedirectStandardOutput = True,
                .RedirectStandardError = True,
                .CreateNoWindow = True
            }
            If Not String.IsNullOrEmpty(workDir) Then psi.WorkingDirectory = workDir
            For i As Integer = 1 To argv.Count - 1
                psi.ArgumentList.Add(argv(i))
            Next
            If extraEnv IsNot Nothing Then
                For Each kv In extraEnv
                    psi.Environment(kv.Key) = kv.Value
                Next
            End If
            Using p As New Process With {.StartInfo = psi}
                Dim sb As New StringBuilder()
                Dim syncLockObj As New Object()
                AddHandler p.OutputDataReceived, Sub(s, e)
                                                     If e.Data IsNot Nothing Then
                                                         SyncLock syncLockObj
                                                             sb.AppendLine(e.Data)
                                                         End SyncLock
                                                     End If
                                                 End Sub
                AddHandler p.ErrorDataReceived, Sub(s, e)
                                                    If e.Data IsNot Nothing Then
                                                        SyncLock syncLockObj
                                                            sb.AppendLine(e.Data)
                                                        End SyncLock
                                                    End If
                                                End Sub
                Try
                    p.Start()
                Catch ex As Exception
                    res.ExitCode = 127
                    res.Output = ex.Message
                    Return res
                End Try
                p.BeginOutputReadLine()
                p.BeginErrorReadLine()
                Dim sw = Stopwatch.StartNew()
                Dim timeoutMs = If(timeoutSec > 0, CLng(timeoutSec) * 1000L, Long.MaxValue)
                While Not p.WaitForExit(200)
                    If monitor IsNot Nothing Then
                        Try
                            monitor()
                        Catch
                            Try : p.Kill(True) : Catch : End Try
                            Throw
                        End Try
                    End If
                    If sw.ElapsedMilliseconds >= timeoutMs Then
                        res.TimedOut = True
                        Try : p.Kill(True) : Catch : End Try
                        Exit While
                    End If
                End While
                If Not res.TimedOut Then
                    p.WaitForExit()
                    res.ExitCode = p.ExitCode
                End If
                SyncLock syncLockObj
                    res.Output = sb.ToString()
                End SyncLock
            End Using
            Return res
        End Function

        Public Function RunPassthrough(argv As IList(Of String),
                                       Optional extraEnv As IDictionary(Of String, String) = Nothing,
                                       Optional workDir As String = Nothing) As Integer
            If argv Is Nothing OrElse argv.Count = 0 Then Return 127
            Dim psi As New ProcessStartInfo With {
                .FileName = argv(0),
                .UseShellExecute = False
            }
            If Not String.IsNullOrEmpty(workDir) Then psi.WorkingDirectory = workDir
            For i As Integer = 1 To argv.Count - 1
                psi.ArgumentList.Add(argv(i))
            Next
            If extraEnv IsNot Nothing Then
                For Each kv In extraEnv
                    psi.Environment(kv.Key) = kv.Value
                Next
            End If
            Try
                Using p = Process.Start(psi)
                    If p Is Nothing Then Return 127
                    p.WaitForExit()
                    Return p.ExitCode
                End Using
            Catch
                Return 127
            End Try
        End Function

        Public Function SpawnDetached(argv As IList(Of String),
                                      Optional extraEnv As IDictionary(Of String, String) = Nothing) As Boolean
            If argv Is Nothing OrElse argv.Count = 0 Then Return False
            Dim psi As New ProcessStartInfo With {
                .FileName = argv(0),
                .UseShellExecute = False
            }
            For i As Integer = 1 To argv.Count - 1
                psi.ArgumentList.Add(argv(i))
            Next
            If extraEnv IsNot Nothing Then
                For Each kv In extraEnv
                    psi.Environment(kv.Key) = kv.Value
                Next
            End If
            Try
                Process.Start(psi)
                Return True
            Catch
                Return False
            End Try
        End Function

        Public Function FileDigest(filePath As String, algorithm As String) As String
            Using fs = File.OpenRead(filePath)
                Dim hashBytes As Byte()
                Select Case algorithm.ToLowerInvariant()
                    Case "sha1"
                        Using h = SHA1.Create()
                            hashBytes = h.ComputeHash(fs)
                        End Using
                    Case Else
                        Using h = SHA256.Create()
                            hashBytes = h.ComputeHash(fs)
                        End Using
                End Select
                Return Convert.ToHexString(hashBytes).ToLowerInvariant()
            End Using
        End Function

        Public Function BytesDigestSha256(text As String) As String
            Dim data = Encoding.UTF8.GetBytes(text)
            Return Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant()
        End Function

        Public Function RandomHex(numBytes As Integer) As String
            Dim buf(numBytes - 1) As Byte
            RandomNumberGenerator.Fill(buf)
            Return Convert.ToHexString(buf).ToLowerInvariant()
        End Function

        Public Sub AtomicWriteText(path As String, content As String, Optional mode As UInteger = &H1A4UI)
            Dim dir = System.IO.Path.GetDirectoryName(path)
            If Not String.IsNullOrEmpty(dir) Then Directory.CreateDirectory(dir)
            Dim tmp = path & ".tmp-" & RandomHex(6)
            File.WriteAllText(tmp, content, New UTF8Encoding(False))
            NativeBridge.chmod(tmp, mode)
            File.Move(tmp, path, True)
        End Sub

        Public Function UnameMachine() As String
            Dim env = Environment.GetEnvironmentVariable("OLYMPIUM_HOST_MACHINE")
            If Not String.IsNullOrEmpty(env) Then Return env
            Dim r = RunCmd({"uname", "-m"}, 5)
            If r.ExitCode = 0 Then Return r.Output.Trim()
            Return RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant()
        End Function

        Public Function UnameRelease() As String
            Dim r = RunCmd({"uname", "-r"}, 5)
            If r.ExitCode = 0 Then Return r.Output.Trim()
            Return Environment.OSVersion.VersionString
        End Function

        Public Function ReadPeMachine(path As String) As UShort
            Try
                Using fs = File.OpenRead(path)
                    Dim hdr(63) As Byte
                    If fs.Read(hdr, 0, 64) <> 64 Then Return 0
                    If hdr(0) <> AscW("M"c) OrElse hdr(1) <> AscW("Z"c) Then Return 0
                    Dim lfanew As Long = CLng(hdr(&H3C)) Or (CLng(hdr(&H3D)) << 8) Or (CLng(hdr(&H3E)) << 16) Or (CLng(hdr(&H3F)) << 24)
                    If lfanew < 64 OrElse lfanew > 1024 * 1024 Then Return 0
                    fs.Seek(lfanew, SeekOrigin.Begin)
                    Dim pe(5) As Byte
                    If fs.Read(pe, 0, 6) <> 6 Then Return 0
                    If pe(0) <> AscW("P"c) OrElse pe(1) <> AscW("E"c) OrElse pe(2) <> 0 OrElse pe(3) <> 0 Then Return 0
                    Return CUShort(CInt(pe(4)) Or (CInt(pe(5)) << 8))
                End Using
            Catch
                Return 0
            End Try
        End Function

        Public Function IsRealPe(path As String) As Boolean
            Return ReadPeMachine(path) <> 0
        End Function

        ' olympium-run accepts ELF, PE, and Mach-O. A shebang script must be executed directly.
        Public Function IsElf(path As String) As Boolean
            Try
                Using fs = File.OpenRead(path)
                    Dim b(3) As Byte
                    If fs.Read(b, 0, 4) <> 4 Then Return False
                    Return b(0) = &H7F AndAlso b(1) = AscW("E"c) AndAlso b(2) = AscW("L"c) AndAlso b(3) = AscW("F"c)
                End Using
            Catch
                Return False
            End Try
        End Function

        Public Function PeMachineName(machine As UShort) As String
            Select Case machine
                Case &H14CUS : Return "32-bit x86"
                Case &H8664US : Return "64-bit x86"
                Case &HAA64US : Return "64-bit ARM"
                Case &H1C0US, &H1C4US : Return "32-bit ARM"
                Case Else : Return "an unknown CPU"
            End Select
        End Function

        Public Function FindXDisplay() As String
            Dim env = Environment.GetEnvironmentVariable("DISPLAY")
            If Not String.IsNullOrEmpty(env) AndAlso env.StartsWith(":") Then
                Dim num = env.Substring(1).Split("."c)(0)
                If File.Exists("/tmp/.X11-unix/X" & num) Then Return env
            End If
            Dim statusFile = "/run/dev-desktop/xwayland-display"
            If File.Exists(statusFile) Then
                Dim d = File.ReadAllText(statusFile).Trim()
                If d.StartsWith(":") Then
                    Dim num = d.Substring(1).Split("."c)(0)
                    If File.Exists("/tmp/.X11-unix/X" & num) Then Return d
                End If
            End If
            For i As Integer = 0 To 9
                If File.Exists("/tmp/.X11-unix/X" & i.ToString()) Then
                    Return ":" & i.ToString()
                End If
            Next
            Return ""
        End Function

        Public Function RealPath(p As String) As String
            If String.IsNullOrEmpty(p) Then Return ""
            Try
                Dim full = Path.GetFullPath(p)
                Dim info As FileSystemInfo = If(Directory.Exists(full), CType(New DirectoryInfo(full), FileSystemInfo), New FileInfo(full))
                Dim resolved = info.ResolveLinkTarget(True)
                If resolved IsNot Nothing Then Return resolved.FullName
                Return full
            Catch
                Return p
            End Try
        End Function

        Public Function ResolveDragonIconPng() As String
            Dim candidates = {
                "/usr/share/olympium/olympium-transparent-1024.png",
                "/usr/share/icons/hicolor/256x256/apps/olympium.png",
                "branding/icons/olympium-transparent-1024.png",
                "../branding/icons/olympium-transparent-1024.png",
                "../../branding/icons/olympium-transparent-1024.png"
            }
            For Each c In candidates
                If File.Exists(c) Then Return c
            Next
            Return ""
        End Function

    End Module

End Namespace
