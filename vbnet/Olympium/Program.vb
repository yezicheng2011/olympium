' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Unified VB.NET (.NET 8) entry point for all Olympium desktop applications
' and system runtime managers. Invoked via lightweight command shims or
' `dotnet Olympium.dll <command> [args...]`.

Imports System
Imports System.IO
Imports System.Linq

Namespace Olympium

    Public Module Program

        Public Function Main(args As String()) As Integer
            Dim app = Environment.GetEnvironmentVariable("OLYMPIUM_APP")
            Dim cmdArgs As String() = args

            If String.IsNullOrEmpty(app) AndAlso args.Length > 0 Then
                app = args(0)
                cmdArgs = If(args.Length > 1, args.Skip(1).ToArray(), Array.Empty(Of String)())
            End If

            If String.IsNullOrEmpty(app) Then
                PrintUsage()
                Return 2
            End If

            app = Path.GetFileName(app)
            Select Case app
                Case "dev-settings" : Return SettingsApp.Run(cmdArgs)
                Case "dev-files" : Return FilesApp.Run(cmdArgs)
                Case "dev-install" : Return InstallApp.Run(cmdArgs)
                Case "dev-swatch" : Return SwatchApp.Run(cmdArgs)
                Case "dev-compat" : Return CompatCli.Run(cmdArgs)
                Case "olympium-testapp" : Return TestApp.Run(cmdArgs)
                Case "olympium-wine-exec" : Return WineAutoRunner.Run(cmdArgs)
                Case "olympium-android" : Return AndroidManager.Run(cmdArgs)
                Case "olympium-openharmony" : Return OpenHarmonyManager.Run(cmdArgs)
                Case "olympium-interop" : Return InteropHub.Run(cmdArgs)
                Case "driver-report" : Return DriverTools.RunReport(cmdArgs)
                Case "driver-install" : Return DriverTools.RunInstall(cmdArgs)
                Case "install-to-disk" : Return DiskInstaller.Run(cmdArgs)
                Case "run-session" : Return SessionRunner.Run(cmdArgs)
                Case "olympium-activate" : Return SystemTools.RunActivate(cmdArgs)
                Case "olympium-launch" : Return SystemTools.RunOlympiumLaunch(cmdArgs)
                Case "olympium-mac-run" : Return SystemTools.RunMacRunner(cmdArgs)
                Case "firefox-launch" : Return SystemTools.RunFirefoxLaunch(cmdArgs)
                Case "guest-fetch" : Return SystemTools.RunGuestFetch(cmdArgs)
                Case "Xwayland-software" : Return SystemTools.RunXwaylandSoftware(cmdArgs)
                Case "olympium-hello32", "olympium-hello64", "olympium-64demo", "olympium-bash64"
                    Return SystemTools.RunBitnessHelper(app)
                Case "--version", "version"
                    Console.WriteLine("Olympium VB.NET Desktop Suite 0.2.0 (.NET 8)")
                    Return 0
                Case Else
                    Console.Error.WriteLine($"Olympium: unknown command '{app}'")
                    PrintUsage()
                    Return 2
            End Select
        End Function

        Private Sub PrintUsage()
            Console.Error.WriteLine("Usage: Olympium <command> [args...]")
            Console.Error.WriteLine("Commands: dev-settings, dev-files, dev-install, dev-swatch, dev-compat, olympium-testapp,")
            Console.Error.WriteLine("          olympium-wine-exec, olympium-android, olympium-openharmony, driver-report,")
            Console.Error.WriteLine("          driver-install, install-to-disk, run-session, olympium-activate, olympium-launch,")
            Console.Error.WriteLine("          olympium-mac-run, firefox-launch, guest-fetch, Xwayland-software")
        End Sub

    End Module

End Namespace
