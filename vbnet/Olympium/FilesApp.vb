' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' File browser in VB.NET (.NET 8) calling native GTK3 & Cairo.
' Clicking a Windows .exe automatically launches it transparently via Wine
' (olympium-wine-exec) with zero manual setup, matching native behavior.

Imports System
Imports System.Collections.Generic
Imports System.IO
Imports System.Text

Namespace Olympium

    Public Module FilesApp
        Private _window As IntPtr
        Private _titleLabel As IntPtr
        Private _statusLabel As IntPtr
        Private _iconFlow As IntPtr
        Private _listScroll As IntPtr
        Private _iconScroll As IntPtr
        Private _backButton As IntPtr
        Private _forwardButton As IntPtr
        Private _store As IntPtr
        Private _iconMode As Boolean = True
        Private _current As String = ""
        Private _filter As String = ""
        Private ReadOnly _backStack As New Stack(Of String)()
        Private ReadOnly _forwardStack As New Stack(Of String)()

        Private Const ITEM_DOCUMENT As Integer = 0
        Private Const ITEM_FOLDER As Integer = 1
        Private Const ITEM_DISK As Integer = 2
        Private Const ITEM_PACKAGE As Integer = 3
        Private Const ITEM_WINDOWS_EXE As Integer = 4

        Private Sub PaintFolder(cr As IntPtr, scale As Double)
            cairo_save(cr)
            cairo_scale(cr, scale, scale)
            cairo_set_source_rgb(cr, 0.22, 0.56, 0.92)
            CairoRoundedRect(cr, 4, 4, 26, 14, 4)
            cairo_fill(cr)
            CairoRoundedRect(cr, 4, 10, 56, 38, 6)
            Dim body = cairo_pattern_create_linear(0, 10, 0, 48)
            cairo_pattern_add_color_stop_rgb(body, 0, 0.50, 0.80, 1.00)
            cairo_pattern_add_color_stop_rgb(body, 1, 0.26, 0.62, 0.97)
            cairo_set_source(cr, body)
            cairo_fill(cr)
            cairo_pattern_destroy(body)
            cairo_set_source_rgba(cr, 1, 1, 1, 0.45)
            CairoRoundedRect(cr, 5, 11, 54, 2, 1)
            cairo_fill(cr)
            cairo_restore(cr)
        End Sub

        Private Sub PaintDocument(cr As IntPtr, scale As Double)
            cairo_save(cr)
            cairo_scale(cr, scale, scale)
            cairo_move_to(cr, 16, 3) : cairo_line_to(cr, 38, 3) : cairo_line_to(cr, 48, 13)
            cairo_line_to(cr, 48, 49) : cairo_line_to(cr, 16, 49) : cairo_close_path(cr)
            cairo_set_source_rgb(cr, 1, 1, 1)
            cairo_fill_preserve(cr)
            cairo_set_source_rgb(cr, 0.78, 0.79, 0.82)
            cairo_set_line_width(cr, 1)
            cairo_stroke(cr)
            cairo_move_to(cr, 38, 3) : cairo_line_to(cr, 38, 13) : cairo_line_to(cr, 48, 13) : cairo_close_path(cr)
            cairo_set_source_rgb(cr, 0.86, 0.87, 0.90)
            cairo_fill(cr)
            cairo_set_source_rgb(cr, 0.84, 0.85, 0.88)
            For i As Integer = 0 To 3
                CairoRoundedRect(cr, 21, 21 + i * 6, If(i = 3, 14, 22), 2, 1)
                cairo_fill(cr)
            Next
            cairo_restore(cr)
        End Sub

        Private Sub PaintDisk(cr As IntPtr)
            CairoRoundedRect(cr, 8, 14, 48, 30, 7)
            Dim body = cairo_pattern_create_linear(0, 14, 0, 44)
            cairo_pattern_add_color_stop_rgb(body, 0, 0.97, 0.97, 0.98)
            cairo_pattern_add_color_stop_rgb(body, 1, 0.82, 0.83, 0.87)
            cairo_set_source(cr, body)
            cairo_fill_preserve(cr)
            cairo_pattern_destroy(body)
            cairo_set_source_rgb(cr, 0.66, 0.67, 0.72)
            cairo_set_line_width(cr, 1)
            cairo_stroke(cr)
            cairo_set_source_rgb(cr, 0.60, 0.62, 0.68)
            CairoRoundedRect(cr, 14, 34, 26, 3, 1.5)
            cairo_fill(cr)
            cairo_set_source_rgb(cr, 0.22, 0.72, 0.36)
            cairo_arc(cr, 48, 35.5, 2.2, 0, 2 * Math.PI)
            cairo_fill(cr)
        End Sub

        Private Sub PaintPackage(cr As IntPtr)
            cairo_set_source_rgb(cr, 0.80, 0.60, 0.36)
            cairo_move_to(cr, 12, 20) : cairo_line_to(cr, 32, 12) : cairo_line_to(cr, 52, 20) : cairo_line_to(cr, 32, 28) : cairo_close_path(cr)
            cairo_fill(cr)
            cairo_set_source_rgb(cr, 0.70, 0.50, 0.28)
            cairo_move_to(cr, 12, 20) : cairo_line_to(cr, 32, 28) : cairo_line_to(cr, 32, 48) : cairo_line_to(cr, 12, 40) : cairo_close_path(cr)
            cairo_fill(cr)
            cairo_set_source_rgb(cr, 0.62, 0.43, 0.23)
            cairo_move_to(cr, 52, 20) : cairo_line_to(cr, 32, 28) : cairo_line_to(cr, 32, 48) : cairo_line_to(cr, 52, 40) : cairo_close_path(cr)
            cairo_fill(cr)
        End Sub

        Private Sub PaintWindowsApp(cr As IntPtr)
            CairoRoundedRect(cr, 10, 6, 44, 40, 8)
            Dim bg = cairo_pattern_create_linear(0, 6, 0, 46)
            cairo_pattern_add_color_stop_rgb(bg, 0, 0.20, 0.58, 0.96)
            cairo_pattern_add_color_stop_rgb(bg, 1, 0.10, 0.38, 0.82)
            cairo_set_source(cr, bg)
            cairo_fill(cr)
            cairo_pattern_destroy(bg)
            cairo_set_source_rgba(cr, 1, 1, 1, 0.92)
            CairoRoundedRect(cr, 19, 15, 11, 10, 2) : cairo_fill(cr)
            CairoRoundedRect(cr, 34, 15, 11, 10, 2) : cairo_fill(cr)
            CairoRoundedRect(cr, 19, 28, 11, 10, 2) : cairo_fill(cr)
            CairoRoundedRect(cr, 34, 28, 11, 10, 2) : cairo_fill(cr)
        End Sub

        Private Function DrawItemIcon(kind As Integer, cr As IntPtr) As Boolean
            Select Case kind
                Case ITEM_FOLDER : PaintFolder(cr, 1.0)
                Case ITEM_DISK : PaintDisk(cr)
                Case ITEM_PACKAGE : PaintPackage(cr)
                Case ITEM_WINDOWS_EXE : PaintWindowsApp(cr)
                Case Else : PaintDocument(cr, 1.0)
            End Select
            Return False
        End Function

        Private Function IsAppBundle(fullPath As String) As Boolean
            Return fullPath.EndsWith(".app", StringComparison.OrdinalIgnoreCase) AndAlso
                   NativeBridge.olympium_mac_is_bundle_dir(fullPath) <> 0
        End Function

        Private Function AppBundlePicture(fullPath As String) As IntPtr
            Dim iconBuf As New StringBuilder(1024)
            If NativeBridge.olympium_mac_bundle_icon_path(fullPath, iconBuf, New UIntPtr(1024UI)) <> 0 Then
                Return IntPtr.Zero
            End If
            Dim srcIcon = iconBuf.ToString()
            If Not File.Exists(srcIcon) Then Return IntPtr.Zero
            Dim fi As New FileInfo(srcIcon)
            Dim key = $"{srcIcon}:{fi.Length}:{fi.LastWriteTimeUtc.Ticks}"
            Dim sum = BytesDigestSha256(key).Substring(0, 24)
            Dim cacheDir = Path.Combine(EnvOr("XDG_CACHE_HOME", Path.Combine(EnvOr("HOME", "/tmp"), ".cache")), "olympium-desk", "icons")
            Directory.CreateDirectory(cacheDir)
            Dim outPng = Path.Combine(cacheDir, sum & ".png")
            If File.Exists(outPng) OrElse NativeBridge.olympium_mac_icon_render(srcIcon, outPng) = 0 Then
                Return gdk_pixbuf_new_from_file_at_scale(outPng, 52, 52, True, IntPtr.Zero)
            End If
            Return IntPtr.Zero
        End Function

        Private Function IsElfOrMachO(path As String) As Integer
            ' Returns 1 for ELF, 2 for Mach-O/Fat, 0 otherwise
            Try
                Using fs = File.OpenRead(path)
                    Dim b(3) As Byte
                    If fs.Read(b, 0, 4) <> 4 Then Return 0
                    If b(0) = &H7F AndAlso b(1) = AscW("E"c) AndAlso b(2) = AscW("L"c) AndAlso b(3) = AscW("F"c) Then Return 1
                    Dim magic As UInteger = (CUInt(b(0)) << 24) Or (CUInt(b(1)) << 16) Or (CUInt(b(2)) << 8) Or CUInt(b(3))
                    If magic = &HFEEDFACEUI OrElse magic = &HCEFAEDFEUI OrElse
                       magic = &HFEEDFACFUI OrElse magic = &HCFFAEDFEUI OrElse
                       magic = &HCAFEBABEUI OrElse magic = &HBEBAFECAUI Then Return 2
                End Using
            Catch
            End Try
            Return 0
        End Function

        Private Sub OpenEntry(name As String)
            Dim nextPath = If(_current = "/", "/" & name, Path.Combine(_current, name))
            If Directory.Exists(nextPath) AndAlso Not IsAppBundle(nextPath) Then
                Go(nextPath)
                Return
            End If
            If Not File.Exists(nextPath) AndAlso Not Directory.Exists(nextPath) Then Return

            ' 1. Windows .exe / .msi -> launch immediately via automatic Wine runner.
            If File.Exists(nextPath) AndAlso (nextPath.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) OrElse
                                              nextPath.EndsWith(".msi", StringComparison.OrdinalIgnoreCase) OrElse
                                              IsRealPe(nextPath)) Then
                Dim wineExec = "/usr/libexec/dev-desktop/olympium-wine-exec"
                If IsExecutable(wineExec) Then
                    SpawnDetached({wineExec, nextPath})
                Else
                    SpawnDetached({"/usr/bin/dev-install", "open", nextPath})
                End If
                gtk_label_set_text(_statusLabel, $"Launching {name} with Wine...")
                Return
            End If

            ' 2. Cross-arch Linux ELF (x86/x86_64/arm32/arm64) or bare Mach-O -> run directly!
            If File.Exists(nextPath) Then
                Dim binKind = IsElfOrMachO(nextPath)
                If binKind = 1 AndAlso IsExecutable(nextPath) Then
                    Dim runner = "/usr/libexec/dev-desktop/olympium-run"
                    If IsExecutable(runner) Then
                        SpawnDetached({runner, nextPath})
                    Else
                        SpawnDetached({nextPath})
                    End If
                    gtk_label_set_text(_statusLabel, $"Launching {name}...")
                    Return
                ElseIf binKind = 2 Then
                    Dim macRunner = "/usr/libexec/dev-desktop/olympium-mac-run"
                    If IsExecutable(macRunner) Then
                        SpawnDetached({macRunner, nextPath, nextPath})
                        gtk_label_set_text(_statusLabel, $"Launching {name} with Darling...")
                        Return
                    End If
                End If
            End If

            ' 3. Packages & app bundles (.deb, .rpm, .dmg, .pkg, .app, .apk, .hap) ->
            '    seamless one-step install & launch on double-click via dev-install open!
            If Not IsExecutable("/usr/bin/dev-install") Then
                gtk_label_set_text(_statusLabel, "Install is not on this image. The file was not run.")
                Return
            End If
            SpawnDetached({"/usr/bin/dev-install", "open", nextPath})
            gtk_label_set_text(_statusLabel, $"Opening {name}...")
        End Sub

        Private Sub AddIconTile(name As String, kind As Integer, fullPath As String)
            Dim btn = gtk_button_new()
            gtk_button_set_relief(btn, GTK_RELIEF_NONE)
            AddCssClass(btn, "item")
            gtk_widget_set_size_request(btn, 112, 96)
            Dim box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4)
            Dim art As IntPtr = IntPtr.Zero
            Dim shown = name
            If kind = ITEM_FOLDER AndAlso IsAppBundle(fullPath) Then
                Dim pb = AppBundlePicture(fullPath)
                If pb <> IntPtr.Zero Then
                    art = gtk_image_new_from_pixbuf(pb)
                    g_object_unref(pb)
                End If
                If shown.Length > 4 Then shown = shown.Substring(0, shown.Length - 4)
            End If
            If art = IntPtr.Zero Then
                art = gtk_drawing_area_new()
                Dim capturedKind = kind
                SignalConnectDraw(art, Function(w, cr, d) DrawItemIcon(capturedKind, cr))
            End If
            gtk_widget_set_size_request(art, 64, 52)
            gtk_widget_set_halign(art, GTK_ALIGN_CENTER)
            Dim lbl = gtk_label_new(shown)
            gtk_label_set_ellipsize(lbl, PANGO_ELLIPSIZE_MIDDLE)
            gtk_label_set_max_width_chars(lbl, 13)
            gtk_box_pack_start(box, art, False, False, 0)
            gtk_box_pack_start(box, lbl, False, False, 0)
            gtk_container_add(btn, box)
            Dim capturedName = name
            SignalConnect(btn, "clicked", Sub(w, d) OpenEntry(capturedName))
            gtk_container_add(_iconFlow, btn)
            gtk_widget_show_all(btn)
        End Sub

        Private Sub Reload()
            gtk_list_store_clear(_store)
            ClearContainer(_iconFlow)
            Dim leaf = Path.GetFileName(_current.TrimEnd("/"c))
            gtk_label_set_text(_titleLabel, If(String.IsNullOrEmpty(leaf), "Computer", leaf))
            gtk_widget_set_sensitive(_backButton, _backStack.Count > 0)
            gtk_widget_set_sensitive(_forwardButton, _forwardStack.Count > 0)

            Dim names As New List(Of String)()
            Try
                Dim showHidden = Not String.IsNullOrEmpty(Environment.GetEnvironmentVariable("OLYMPIUM_SHOW_HIDDEN"))
                For Each entry In Directory.EnumerateFileSystemEntries(_current)
                    Dim n = Path.GetFileName(entry)
                    If n.StartsWith(".") AndAlso Not showHidden Then Continue For
                    If Not String.IsNullOrEmpty(_filter) AndAlso Not n.Contains(_filter, StringComparison.OrdinalIgnoreCase) Then Continue For
                    names.Add(n)
                Next
            Catch
                gtk_label_set_text(_statusLabel, "This folder cannot be opened.")
                Return
            End Try
            names.Sort(StringComparer.OrdinalIgnoreCase)

            For i As Integer = 0 To names.Count - 1
                Dim n = names(i)
                Dim full = If(_current = "/", "/" & n, Path.Combine(_current, n))
                Dim kindStr = "Document"
                Dim sizeStr = "--"
                Dim iconKind = ITEM_DOCUMENT
                Try
                    If Directory.Exists(full) Then
                        kindStr = If(IsAppBundle(full), "Application", "Folder")
                        iconKind = ITEM_FOLDER
                    ElseIf File.Exists(full) Then
                        Dim fi As New FileInfo(full)
                        If n.EndsWith(".dmg", StringComparison.OrdinalIgnoreCase) Then
                            kindStr = "Disk Image" : iconKind = ITEM_DISK
                        ElseIf n.EndsWith(".pkg", StringComparison.OrdinalIgnoreCase) OrElse
                               n.EndsWith(".deb", StringComparison.OrdinalIgnoreCase) OrElse
                               n.EndsWith(".rpm", StringComparison.OrdinalIgnoreCase) OrElse
                               n.EndsWith(".apk", StringComparison.OrdinalIgnoreCase) OrElse
                               n.EndsWith(".hap", StringComparison.OrdinalIgnoreCase) Then
                            kindStr = "Installer Package" : iconKind = ITEM_PACKAGE
                        ElseIf n.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) Then
                            kindStr = "Windows Program" : iconKind = ITEM_WINDOWS_EXE
                        ElseIf IsExecutable(full) Then
                            kindStr = "Program"
                        End If
                        If fi.Length >= 1024L * 1024L Then
                            sizeStr = $"{fi.Length / 1048576.0:F1} MB"
                        ElseIf fi.Length >= 1024L Then
                            sizeStr = $"{fi.Length \ 1024L} KB"
                        Else
                            sizeStr = $"{fi.Length} bytes"
                        End If
                    End If
                Catch
                End Try
                Dim iter As GtkTreeIter
                gtk_list_store_append(_store, iter)
                gtk_list_store_set(_store, iter, 0, n, 1, kindStr, 2, sizeStr, -1)
                If i < 400 Then AddIconTile(n, iconKind, full)
            Next

            If names.Count = 0 Then
                gtk_label_set_text(_statusLabel, If(String.IsNullOrEmpty(_filter), "This folder is empty", "No matches"))
            Else
                gtk_label_set_text(_statusLabel, $"{names.Count} {If(names.Count = 1, "item", "items")}")
            End If
        End Sub

        Private Sub Go(path As String)
            If Not String.IsNullOrEmpty(_current) Then _backStack.Push(_current)
            _forwardStack.Clear()
            _current = RealPath(path)
            Reload()
        End Sub

        Private Function MakePlaceButton(label As String, path As String) As IntPtr
            Dim btn = gtk_button_new_with_label(label)
            gtk_button_set_relief(btn, GTK_RELIEF_NONE)
            AddCssClass(btn, "place")
            Dim target = path
            SignalConnect(btn, "clicked", Sub(w, d) Go(target))
            Return btn
        End Function

        Private Function MakeSectionLabel(text As String) As IntPtr
            Dim lbl = gtk_label_new(text)
            AddCssClass(lbl, "section")
            gtk_widget_set_halign(lbl, GTK_ALIGN_START)
            gtk_widget_set_margin_start(lbl, 10)
            gtk_widget_set_margin_top(lbl, 10)
            gtk_widget_set_margin_bottom(lbl, 2)
            Return lbl
        End Function

        Public Function Run(args As String()) As Integer
            Dim argc = 0
            Dim argv = IntPtr.Zero
            gtk_init(argc, argv)
            g_set_prgname("dev-files")
            g_object_set(gtk_settings_get_default(), "gtk-font-name", "Ubuntu 12", IntPtr.Zero)
            LoadCss(
                "window { background: #ffffff; }" &
                "label { color: #1d1d1f; }" &
                ".sidebar { background: #ecedf0; border-right: 1px solid #d9d9de; }" &
                ".section { color: #8a8a8f; font-weight: bold; font-size: 11px; }" &
                "button.place { padding: 4px 8px; margin: 0 6px; border-radius: 7px; border: none; background: none; box-shadow: none; }" &
                "button.place:hover { background: rgba(0,0,0,0.07); }" &
                ".toolbar { background: #f6f6f8; border-bottom: 1px solid #dedee2; }" &
                ".title { font-weight: bold; font-size: 14px; }" &
                "button.tool { padding: 5px 7px; border-radius: 7px; border: none; background: none; box-shadow: none; }" &
                "button.tool:hover { background: rgba(0,0,0,0.08); }" &
                "button.item { padding: 6px 2px; border-radius: 9px; border: none; background: none; box-shadow: none; }" &
                "button.item:hover { background: rgba(41,120,245,0.14); }" &
                "button.item label { font-size: 12px; }" &
                "entry { border-radius: 8px; border: 1px solid #d2d2d7; background: #ffffff; color: #1d1d1f; padding: 2px 6px; min-height: 24px; }" &
                ".status { color: #6e6e73; font-size: 11px; background: #f6f6f8; border-top: 1px solid #dedee2; padding: 3px; }" &
                "treeview { background: #ffffff; color: #1d1d1f; }" &
                "treeview:selected { background: #2978f5; color: #ffffff; }")

            _window = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_decorated(_window, False)
            gtk_window_set_default_size(_window, 920, 540)
            gtk_window_set_title(_window, "Files")
            SignalConnect(_window, "destroy", Sub(w, d) gtk_main_quit())

            Dim root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)
            gtk_container_add(_window, root)

            Dim side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1)
            AddCssClass(side, "sidebar")
            gtk_widget_set_size_request(side, 190, -1)
            gtk_box_pack_start(root, side, False, False, 0)
            Dim home = EnvOr("HOME", "/")
            gtk_box_pack_start(side, MakeSectionLabel("Favorites"), False, False, 0)
            gtk_box_pack_start(side, MakePlaceButton("Home", home), False, False, 0)
            gtk_box_pack_start(side, MakePlaceButton("Documents", "/usr/share/doc"), False, False, 0)
            gtk_box_pack_start(side, MakePlaceButton("Downloads", "/tmp"), False, False, 0)
            gtk_box_pack_start(side, MakePlaceButton("Applications", "/usr/bin"), False, False, 0)
            gtk_box_pack_start(side, MakeSectionLabel("Locations"), False, False, 0)
            gtk_box_pack_start(side, MakePlaceButton("Computer", "/"), False, False, 0)
            gtk_box_pack_start(side, MakePlaceButton("System", "/usr"), False, False, 0)

            Dim mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)
            gtk_box_pack_start(root, mainBox, True, True, 0)

            Dim toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4)
            AddCssClass(toolbar, "toolbar")
            Dim inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4)
            gtk_widget_set_margin_start(inner, 10) : gtk_widget_set_margin_end(inner, 10)
            gtk_widget_set_margin_top(inner, 7) : gtk_widget_set_margin_bottom(inner, 7)
            gtk_box_pack_start(toolbar, inner, True, True, 0)

            _backButton = gtk_button_new_with_label("<")
            gtk_button_set_relief(_backButton, GTK_RELIEF_NONE) : AddCssClass(_backButton, "tool")
            SignalConnect(_backButton, "clicked", Sub(w, d)
                                                      If _backStack.Count = 0 Then Return
                                                      _forwardStack.Push(_current)
                                                      _current = _backStack.Pop()
                                                      Reload()
                                                  End Sub)
            _forwardButton = gtk_button_new_with_label(">")
            gtk_button_set_relief(_forwardButton, GTK_RELIEF_NONE) : AddCssClass(_forwardButton, "tool")
            SignalConnect(_forwardButton, "clicked", Sub(w, d)
                                                         If _forwardStack.Count = 0 Then Return
                                                         _backStack.Push(_current)
                                                         _current = _forwardStack.Pop()
                                                         Reload()
                                                     End Sub)
            _titleLabel = gtk_label_new("")
            AddCssClass(_titleLabel, "title")
            gtk_widget_set_margin_start(_titleLabel, 8)
            gtk_label_set_ellipsize(_titleLabel, PANGO_ELLIPSIZE_END)

            Dim search = gtk_search_entry_new()
            gtk_entry_set_placeholder_text(search, "Search")
            gtk_widget_set_size_request(search, 180, -1)
            SignalConnect(search, "search-changed", Sub(w, d)
                                                        _filter = PtrToUtf8(gtk_entry_get_text(search))
                                                        Reload()
                                                    End Sub)
            Dim btnGrid = gtk_button_new_with_label("Icons")
            gtk_button_set_relief(btnGrid, GTK_RELIEF_NONE) : AddCssClass(btnGrid, "tool")
            SignalConnect(btnGrid, "clicked", Sub(w, d)
                                                  _iconMode = True
                                                  gtk_widget_set_visible(_iconScroll, True)
                                                  gtk_widget_set_visible(_listScroll, False)
                                              End Sub)
            Dim btnList = gtk_button_new_with_label("List")
            gtk_button_set_relief(btnList, GTK_RELIEF_NONE) : AddCssClass(btnList, "tool")
            SignalConnect(btnList, "clicked", Sub(w, d)
                                                  _iconMode = False
                                                  gtk_widget_set_visible(_iconScroll, False)
                                                  gtk_widget_set_visible(_listScroll, True)
                                              End Sub)

            gtk_box_pack_start(inner, _backButton, False, False, 0)
            gtk_box_pack_start(inner, _forwardButton, False, False, 0)
            gtk_box_pack_start(inner, _titleLabel, False, False, 0)
            gtk_box_pack_end(inner, search, False, False, 0)
            gtk_box_pack_end(inner, btnList, False, False, 0)
            gtk_box_pack_end(inner, btnGrid, False, False, 0)
            gtk_box_pack_start(mainBox, toolbar, False, False, 0)

            Dim strType = g_type_from_name("gchararray")
            _store = gtk_list_store_newv(3, {strType, strType, strType})
            Dim list = gtk_tree_view_new_with_model(_store)
            gtk_tree_view_insert_column_with_attributes(list, -1, "Name", gtk_cell_renderer_text_new(), "text", 0, IntPtr.Zero)
            gtk_tree_view_insert_column_with_attributes(list, -1, "Kind", gtk_cell_renderer_text_new(), "text", 1, IntPtr.Zero)
            gtk_tree_view_insert_column_with_attributes(list, -1, "Size", gtk_cell_renderer_text_new(), "text", 2, IntPtr.Zero)
            gtk_tree_view_column_set_expand(gtk_tree_view_get_column(list, 0), True)
            g_signal_connect_data(list, "row-activated", PinDelegate(New TreeRowActivatedCallback(
                Sub(tv, tpath, col, d)
                    Dim model = gtk_tree_view_get_model(tv)
                    Dim iter As GtkTreeIter
                    If gtk_tree_model_get_iter(model, iter, tpath) Then
                        Dim namePtr As IntPtr = IntPtr.Zero
                        gtk_tree_model_get(model, iter, 0, namePtr, -1)
                        If namePtr <> IntPtr.Zero Then
                            Dim nm = PtrToUtf8(namePtr)
                            g_free(namePtr)
                            OpenEntry(nm)
                        End If
                    End If
                End Sub)), IntPtr.Zero, IntPtr.Zero, 0)

            _listScroll = gtk_scrolled_window_new(IntPtr.Zero, IntPtr.Zero)
            gtk_container_add(_listScroll, list)

            _iconFlow = gtk_flow_box_new()
            gtk_flow_box_set_selection_mode(_iconFlow, GTK_SELECTION_NONE)
            gtk_flow_box_set_max_children_per_line(_iconFlow, 12UI)
            gtk_flow_box_set_homogeneous(_iconFlow, True)
            gtk_widget_set_valign(_iconFlow, GTK_ALIGN_START)
            gtk_widget_set_margin_start(_iconFlow, 14) : gtk_widget_set_margin_end(_iconFlow, 14) : gtk_widget_set_margin_top(_iconFlow, 12)
            _iconScroll = gtk_scrolled_window_new(IntPtr.Zero, IntPtr.Zero)
            gtk_scrolled_window_set_policy(_iconScroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC)
            gtk_container_add(_iconScroll, _iconFlow)

            gtk_box_pack_start(mainBox, _iconScroll, True, True, 0)
            gtk_box_pack_start(mainBox, _listScroll, True, True, 0)

            _statusLabel = gtk_label_new("")
            AddCssClass(_statusLabel, "status")
            gtk_box_pack_start(mainBox, _statusLabel, False, False, 0)

            _current = RealPath(If(args.Length > 0 AndAlso Not String.IsNullOrEmpty(args(0)), args(0), home))
            gtk_widget_show_all(_window)
            gtk_widget_hide(_listScroll)
            Reload()
            gtk_main()
            Return 0
        End Function
    End Module

End Namespace
