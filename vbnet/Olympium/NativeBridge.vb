' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' P/Invoke bindings to libolympium-native.so (confinement, libcrypt accounts,
' Mach-O / DMG / PKG / ICNS native decoders, Cairo dragon mark, compositor IPC)
' and POSIX libc helpers.

Imports System
Imports System.Runtime.InteropServices
Imports System.Text

Namespace Olympium

    Public Module NativeBridge
        Private Const NativeLib As String = "libolympium-native.so"
        Private Const LibC As String = "libc"

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub olympium_draw_dragon(cr As IntPtr, x As Double, y As Double, size As Double, r As Double, g As Double, b As Double)
        End Sub

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_send_compositor_cmd(cmd As String) As Integer
        End Function

        ' ---- Confinement ----
        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function olympium_confine_probe() As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function olympium_confine_name(how As Integer) As IntPtr
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_confined_run(argv As IntPtr(), readPath As String, writeDir As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_confined_pipe(producer As IntPtr(), readPath As String, consumer As IntPtr(), writeDir As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub olympium_tree_settle(root As String)
        End Sub

        ' ---- Mach-O / Mac bundle / DMG / PKG ----
        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function olympium_macho_probe(data As Byte(), length As UIntPtr, descOut As StringBuilder, descCap As UIntPtr) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_detect(path As String) As IntPtr
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_is_bundle_dir(path As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_bundle_icon_path(bundleDir As String, outBuf As StringBuilder, cap As UIntPtr) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_icon_render(srcIcon As String, destPng As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub olympium_mac_clear_stale(deskRoot As String)
        End Sub

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_bundle_check_launch(bundleDir As String, ByRef isMacho As Integer, ByRef canRun As Integer,
                                                         archsOut As StringBuilder, archsCap As UIntPtr,
                                                         hostOut As StringBuilder, hostCap As UIntPtr) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_review_json(path As String, cacheDir As String, jsonOut As StringBuilder, cap As UIntPtr) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_mac_install_json(path As String, deskRoot As String, jsonOut As StringBuilder, cap As UIntPtr,
                                                  errOut As StringBuilder, errCap As UIntPtr) As Integer
        End Function

        ' ---- Accounts ----
        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_name_ok(name As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_password_ok(pw As String, why As StringBuilder, n As UIntPtr) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_exists(name As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_has_password(name As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_check_password(name As String, pw As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_set_password(name As String, pw As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_create(name As String, full As String, pw As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function olympium_acct_setup_done() As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_mark_setup_done(name As String) As Integer
        End Function

        <DllImport(NativeLib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function olympium_acct_primary(outBuf As StringBuilder, n As UIntPtr) As Integer
        End Function

        ' ---- POSIX libc ----
        <DllImport(LibC, CallingConvention:=CallingConvention.Cdecl)>
        Public Function getuid() As UInteger
        End Function

        <DllImport(LibC, CallingConvention:=CallingConvention.Cdecl)>
        Public Function geteuid() As UInteger
        End Function

        <DllImport(LibC, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function chmod(path As String, mode As UInteger) As Integer
        End Function

        <DllImport(LibC, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function access(path As String, mode As Integer) As Integer
        End Function

        <DllImport(LibC, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function chown(path As String, owner As UInteger, group As UInteger) As Integer
        End Function

        <DllImport(LibC, CallingConvention:=CallingConvention.Cdecl)>
        Public Function sync() As Integer
        End Function

        Public Const F_OK As Integer = 0
        Public Const X_OK As Integer = 1
        Public Const W_OK As Integer = 2
        Public Const R_OK As Integer = 4

        Public Function PtrToUtf8(p As IntPtr) As String
            If p = IntPtr.Zero Then Return ""
            Return Marshal.PtrToStringUTF8(p)
        End Function

        Public Function ConfineName() As String
            Return PtrToUtf8(olympium_confine_name(olympium_confine_probe()))
        End Function

        Public Function MacDetect(path As String) As String
            Return PtrToUtf8(olympium_mac_detect(path))
        End Function

        Public Function RunConfined(argv As String(), readPath As String, writeDir As String) As Integer
            Dim ptrs(argv.Length) As IntPtr
            Try
                For i As Integer = 0 To argv.Length - 1
                    ptrs(i) = Marshal.StringToCoTaskMemUTF8(argv(i))
                Next
                ptrs(argv.Length) = IntPtr.Zero
                Return olympium_confined_run(ptrs, readPath, writeDir)
            Finally
                For i As Integer = 0 To argv.Length - 1
                    If ptrs(i) <> IntPtr.Zero Then Marshal.FreeCoTaskMem(ptrs(i))
                Next
            End Try
        End Function

        Public Function RunConfinedPipe(producer As String(), readPath As String, consumer As String(), writeDir As String) As Integer
            Dim pPtrs(producer.Length) As IntPtr
            Dim cPtrs(consumer.Length) As IntPtr
            Try
                For i As Integer = 0 To producer.Length - 1
                    pPtrs(i) = Marshal.StringToCoTaskMemUTF8(producer(i))
                Next
                pPtrs(producer.Length) = IntPtr.Zero
                For i As Integer = 0 To consumer.Length - 1
                    cPtrs(i) = Marshal.StringToCoTaskMemUTF8(consumer(i))
                Next
                cPtrs(consumer.Length) = IntPtr.Zero
                Return olympium_confined_pipe(pPtrs, readPath, cPtrs, writeDir)
            Finally
                For i As Integer = 0 To producer.Length - 1
                    If pPtrs(i) <> IntPtr.Zero Then Marshal.FreeCoTaskMem(pPtrs(i))
                Next
                For i As Integer = 0 To consumer.Length - 1
                    If cPtrs(i) <> IntPtr.Zero Then Marshal.FreeCoTaskMem(cPtrs(i))
                Next
            End Try
        End Function
    End Module

End Namespace
