' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Lawful Test App (olympium-testapp), Live Color Field (dev-swatch),
' and Compatibility Matrix / Mach-O probe CLI (dev-compat) in VB.NET (.NET 8).

Imports System
Imports System.IO
Imports System.Text

Namespace Olympium

    Public Module TestApp
        Private Const BodyText As String =
            "This window is a Linux program written for Olympium (VB.NET on .NET 8). " &
            "It is not a Mac application. It was not built with an Apple SDK, " &
            "and it does not contain Apple software." & vbLf & vbLf &
            "Launchpad, the dock, and Files open this program directly. " &
            "No Apple operating system, framework, font, or artwork is included. " &
            "Code signatures and DRM are not bypassed." & vbLf & vbLf &
            "iOS is not part of this system."

        Public Function Run(args As String()) As Integer
            Dim argc = 0
            Dim argv = IntPtr.Zero
            gtk_init(argc, argv)
            g_set_prgname("olympium-testapp")
            Dim win = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_title(win, "Lawful test app")
            gtk_window_set_default_size(win, 680, 460)
            SignalConnect(win, "destroy", Sub(w, d) gtk_main_quit())

            Dim box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14)
            gtk_container_set_border_width(box, 28UI)
            gtk_container_add(win, box)

            Dim title = gtk_label_new(Nothing)
            gtk_label_set_markup(title, "<span size='xx-large' weight='bold'>Lawful test app</span>")
            gtk_widget_set_halign(title, GTK_ALIGN_START)
            gtk_box_pack_start(box, title, False, False, 0)

            Dim body = gtk_label_new(BodyText)
            gtk_label_set_line_wrap(body, True)
            gtk_label_set_xalign(body, 0.0F)
            gtk_label_set_max_width_chars(body, 58)
            gtk_label_set_selectable(body, True)
            gtk_box_pack_start(box, body, True, True, 0)

            Dim closeBtn = gtk_button_new_with_label("Close")
            gtk_widget_set_halign(closeBtn, GTK_ALIGN_END)
            SignalConnect(closeBtn, "clicked", Sub(w, d) gtk_main_quit())
            gtk_box_pack_start(box, closeBtn, False, False, 0)

            gtk_widget_show_all(win)
            gtk_main()
            Return 0
        End Function
    End Module

    Public Module SwatchApp
        Private _offset As Integer = 0

        Public Function Run(args As String()) As Integer
            Dim argc = 0
            Dim argv = IntPtr.Zero
            gtk_init(argc, argv)
            g_set_prgname("dev-swatch")
            Dim win = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_title(win, "Color field")
            gtk_window_set_default_size(win, 980, 260)
            SignalConnect(win, "destroy", Sub(w, d) gtk_main_quit())

            Dim area = gtk_drawing_area_new()
            Dim colors As Double(,) = {
                {0.85, 0.18, 0.22},
                {0.15, 0.45, 0.82},
                {0.16, 0.62, 0.34},
                {0.92, 0.72, 0.18},
                {0.45, 0.22, 0.62}
            }
            SignalConnectDraw(area, Function(widget, cr, d)
                                        Dim width = gtk_widget_get_allocated_width(widget)
                                        Dim height = gtk_widget_get_allocated_height(widget)
                                        Dim stripe = 48
                                        For x As Integer = -stripe To width + stripe Step stripe
                                            Dim idx = ((x + _offset) \ stripe) Mod 5
                                            If idx < 0 Then idx += 5
                                            cairo_set_source_rgb(cr, colors(idx, 0), colors(idx, 1), colors(idx, 2))
                                            cairo_rectangle(cr, x - (_offset Mod stripe), 0, stripe, height)
                                            cairo_fill(cr)
                                        Next
                                        cairo_set_source_rgba(cr, 1, 1, 1, 0.92)
                                        cairo_select_font_face(cr, "Ubuntu", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD)
                                        cairo_set_font_size(cr, 22)
                                        cairo_move_to(cr, 16, 36)
                                        cairo_show_text(cr, "Live color field")
                                        Return False
                                    End Function)
            gtk_container_add(win, area)
            g_timeout_add(80UI, PinDelegate(New TimeoutCallback(Function(d)
                                                                    gtk_widget_queue_draw(area)
                                                                    _offset = (_offset + 6) Mod 240
                                                                    Return True
                                                                End Function)), IntPtr.Zero)
            gtk_widget_show_all(win)
            gtk_main()
            Return 0
        End Function
    End Module

    Public Module CompatCli
        Private Sub PrintMatrix()
            Console.WriteLine("Host architecture is native. Instruction translation is a separate layer.")
            Console.WriteLine()
            Console.WriteLine("layer                  macOS GUI app")
            Console.WriteLine("ISA translation        needed only when the CPU differs")
            Console.WriteLine("Mach-O / fat loader    required, not in this image")
            Console.WriteLine("dyld and libSystem     required, Apple's are not bundled")
            Console.WriteLine("Objective-C runtime    required, not bundled")
            Console.WriteLine("AppKit                 required, not bundled")
            Console.WriteLine("this image             opens a .dmg, .pkg, or .app and copies the application into Applications")
            Console.WriteLine("                       starts it only through Darling, when Darling is installed")
            Console.WriteLine("                       a bare Mach-O file is identified and not executed")
            Console.WriteLine("Olympium runtime       does not start macOS software on Linux")
            Console.WriteLine("lawful test app        a Linux window, not a Mac application")
            Console.WriteLine()
            Console.WriteLine("iOS is not part of this system.")
            Console.WriteLine("A native Linux executable, including one built here, is not a Mac application.")
            Console.WriteLine("Pure software instruction translation does not implement Mach-O, dyld, AppKit, or UIKit.")
            Console.WriteLine("This tool does not bypass code signatures, DRM, or FairPlay, and it does not download applications.")
        End Sub

        Private Function ProbeFile(path As String) As Integer
            Dim buf(63) As Byte
            Dim n As Integer
            Try
                Using fs = File.OpenRead(path)
                    n = fs.Read(buf, 0, buf.Length)
                End Using
            Catch ex As Exception
                Console.Error.WriteLine($"{path}: {ex.Message}")
                Return 1
            End Try
            Dim desc As New StringBuilder(200)
            If NativeBridge.olympium_macho_probe(buf, New UIntPtr(CUInt(n)), desc, New UIntPtr(200UI)) = 0 Then
                Console.WriteLine($"{path}: {desc}")
                Console.WriteLine("Not executed. This image has no Mach-O loader, dyld, libSystem, or AppKit.")
                Console.WriteLine("The Olympium runtime does not start macOS software on Linux.")
                Console.WriteLine("iOS is not part of this system.")
                Return 3
            End If
            If n >= 4 AndAlso buf(0) = &H7F AndAlso buf(1) = AscW("E"c) AndAlso buf(2) = AscW("L"c) AndAlso buf(3) = AscW("F"c) Then
                Console.WriteLine($"{path}: Linux ELF. This is not a Mach-O, AppKit, or UIKit application.")
                Return 0
            End If
            If n >= 2 AndAlso buf(0) = AscW("P"c) AndAlso buf(1) = AscW("K"c) Then
                Console.WriteLine($"{path}: zip container. Not launched.")
                Return 3
            End If
            Console.WriteLine($"{path}: unrecognized. Not launched.")
            Return 2
        End Function

        Public Function Run(args As String()) As Integer
            If args.Length = 0 OrElse args(0) = "matrix" Then
                PrintMatrix()
                Return 0
            End If
            If args(0) = "probe" AndAlso args.Length = 2 Then
                Return ProbeFile(args(1))
            End If
            If args(0) = "launch" AndAlso args.Length = 2 Then
                Dim rc = ProbeFile(args(1))
                If rc = 0 Then
                    Console.WriteLine("Refusing to treat a Linux ELF as a Mac application.")
                    Return 2
                End If
                Return rc
            End If
            Console.Error.WriteLine("Usage: dev-compat matrix | probe FILE | launch FILE")
            Return 1
        End Function
    End Module

End Namespace
