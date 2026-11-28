' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Settings desktop application in VB.NET (.NET 8) calling native GTK3 & Cairo.
' Uses the unified latest Olympium dragon mark in the sidebar.

Imports System
Imports System.Collections.Generic
Imports System.IO
Imports System.Text

Namespace Olympium

    Public Module SettingsApp
        Private _stack As IntPtr
        Private _emptyLabel As IntPtr
        Private ReadOnly _navItems As New List(Of (Id As String, Label As String, Button As IntPtr))()
        Private _dragonPixbuf As IntPtr = IntPtr.Zero
        Private _dragonTried As Boolean = False

        Private Function LoadDragonPixbuf() As IntPtr
            If _dragonTried Then Return _dragonPixbuf
            _dragonTried = True
            Dim png = ResolveDragonIconPng()
            If Not String.IsNullOrEmpty(png) Then
                _dragonPixbuf = gdk_pixbuf_new_from_file_at_scale(png, 22, 22, True, IntPtr.Zero)
            End If
            Return _dragonPixbuf
        End Function

        Private Function ReadFileOr(path As String, missing As String) As String
            Try
                If File.Exists(path) Then Return File.ReadAllText(path)
            Catch
            End Try
            Return missing
        End Function

        Private Sub IconFill(cr As IntPtr, r1 As Double, g1 As Double, b1 As Double, r2 As Double, g2 As Double, b2 As Double)
            CairoRoundedRect(cr, 1, 1, 26, 26, 6.5)
            Dim bg = cairo_pattern_create_linear(0, 0, 0, 28)
            cairo_pattern_add_color_stop_rgb(bg, 0, r1, g1, b1)
            cairo_pattern_add_color_stop_rgb(bg, 1, r2, g2, b2)
            cairo_set_source(cr, bg)
            cairo_fill(cr)
            cairo_pattern_destroy(bg)
        End Sub

        Private Function DrawNavIcon(id As String, cr As IntPtr) As Boolean
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND)
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND)
            Select Case id
                Case "users"
                    IconFill(cr, 0.55, 0.60, 0.68, 0.36, 0.40, 0.48)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.95)
                    cairo_arc(cr, 14, 10.5, 4.2, 0, 6.2832)
                    cairo_fill(cr)
                    cairo_arc(cr, 14, 23, 8, 3.1416, 6.2832)
                    cairo_fill(cr)
                Case "appearance"
                    IconFill(cr, 0.36, 0.62, 1.00, 0.18, 0.36, 0.90)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.95)
                    CairoRoundedRect(cr, 6, 7, 16, 12, 2)
                    cairo_fill(cr)
                    cairo_rectangle(cr, 12, 19, 4, 3)
                    cairo_fill(cr)
                    CairoRoundedRect(cr, 8, 21, 12, 2.2, 1)
                    cairo_fill(cr)
                Case "drivers"
                    IconFill(cr, 1.00, 0.72, 0.28, 0.94, 0.42, 0.12)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.95)
                    CairoRoundedRect(cr, 7, 8, 14, 12, 2.4)
                    cairo_fill(cr)
                    cairo_set_source_rgba(cr, 0.85, 0.40, 0.10, 1)
                    For i As Integer = 0 To 3
                        cairo_rectangle(cr, 9 + i * 3.2, 5.2, 1.6, 3)
                        cairo_rectangle(cr, 9 + i * 3.2, 19.6, 1.6, 3)
                        cairo_fill(cr)
                    Next
                Case "compatibility"
                    IconFill(cr, 0.70, 0.48, 0.98, 0.40, 0.24, 0.86)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.55)
                    CairoRoundedRect(cr, 6, 6, 12, 12, 3)
                    cairo_fill(cr)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.95)
                    CairoRoundedRect(cr, 10, 10, 12, 12, 3)
                    cairo_fill(cr)
                Case "olympium"
                    ' Unified latest dragon artwork matching Dock, Launchpad, and Splash.
                    IconFill(cr, 0.11, 0.12, 0.15, 0.02, 0.02, 0.03)
                    Dim pb = LoadDragonPixbuf()
                    If pb <> IntPtr.Zero Then
                        gdk_cairo_set_source_pixbuf(cr, pb, 3.0, 3.0)
                        cairo_paint(cr)
                    Else
                        NativeBridge.olympium_draw_dragon(cr, 3.0, 3.0, 22.0, 1.00, 0.92, 0.54)
                    End If
                Case "license"
                    IconFill(cr, 0.36, 0.82, 0.52, 0.12, 0.62, 0.36)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.96)
                    CairoRoundedRect(cr, 8, 6, 12, 16, 2)
                    cairo_fill(cr)
                    cairo_set_source_rgba(cr, 0.16, 0.58, 0.32, 0.9)
                    cairo_set_line_width(cr, 1.4)
                    cairo_move_to(cr, 10.5, 11) : cairo_line_to(cr, 17.5, 11)
                    cairo_move_to(cr, 10.5, 14.5) : cairo_line_to(cr, 16, 14.5)
                    cairo_stroke(cr)
                Case Else
                    IconFill(cr, 0.62, 0.64, 0.70, 0.38, 0.40, 0.46)
                    cairo_set_source_rgba(cr, 1, 1, 1, 0.96)
                    cairo_arc(cr, 14, 14, 8, 0, 6.2832)
                    cairo_fill(cr)
                    cairo_set_source_rgba(cr, 0.40, 0.42, 0.48, 1)
                    cairo_arc(cr, 14, 11.2, 1.15, 0, 6.2832)
                    cairo_fill(cr)
                    cairo_rectangle(cr, 13.2, 13.4, 1.6, 5.2)
                    cairo_fill(cr)
            End Select
            Return False
        End Function

        Private Function MakePane(heading As String) As IntPtr
            Dim box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14)
            gtk_widget_set_margin_top(box, 18)
            gtk_widget_set_margin_start(box, 28)
            gtk_widget_set_margin_end(box, 28)
            gtk_widget_set_margin_bottom(box, 22)
            Dim title = gtk_label_new(heading)
            AddCssClass(title, "heading")
            gtk_widget_set_halign(title, GTK_ALIGN_START)
            gtk_box_pack_start(box, title, False, False, 0)
            Return box
        End Function

        Private Function MakeCard() As IntPtr
            Dim box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0)
            AddCssClass(box, "card")
            Return box
        End Function

        Private Sub AddSepIfNeeded(box As IntPtr)
            Dim children = gtk_container_get_children(box)
            Dim count = g_list_length(children)
            If children <> IntPtr.Zero Then g_list_free(children)
            If count = 0 Then Return
            Dim sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL)
            AddCssClass(sep, "rowsep")
            gtk_widget_set_margin_start(sep, 16)
            gtk_box_pack_start(box, sep, False, False, 0)
        End Sub

        Private Sub AddText(box As IntPtr, text As String, Optional cssClass As String = Nothing)
            AddSepIfNeeded(box)
            Dim lbl = gtk_label_new(text)
            gtk_label_set_xalign(lbl, 0.0F)
            gtk_label_set_line_wrap(lbl, True)
            gtk_label_set_max_width_chars(lbl, 56)
            gtk_label_set_selectable(lbl, True)
            gtk_widget_set_margin_start(lbl, 16)
            gtk_widget_set_margin_end(lbl, 16)
            gtk_widget_set_margin_top(lbl, 10)
            gtk_widget_set_margin_bottom(lbl, 10)
            If Not String.IsNullOrEmpty(cssClass) Then AddCssClass(lbl, cssClass)
            gtk_box_pack_start(box, lbl, False, False, 0)
        End Sub

        Private Sub AddNote(box As IntPtr, text As String)
            Dim lbl = gtk_label_new(text)
            gtk_label_set_xalign(lbl, 0.0F)
            gtk_label_set_line_wrap(lbl, True)
            gtk_label_set_max_width_chars(lbl, 58)
            AddCssClass(lbl, "note")
            gtk_widget_set_margin_start(lbl, 16)
            gtk_widget_set_margin_end(lbl, 8)
            gtk_widget_set_margin_top(lbl, -6)
            gtk_box_pack_start(box, lbl, False, False, 0)
        End Sub

        Private Function AddRow(box As IntPtr, name As String, value As String) As IntPtr
            AddSepIfNeeded(box)
            Dim row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12)
            gtk_widget_set_margin_start(row, 16)
            gtk_widget_set_margin_end(row, 14)
            gtk_widget_set_margin_top(row, 9)
            gtk_widget_set_margin_bottom(row, 9)
            Dim left = gtk_label_new(name)
            gtk_widget_set_halign(left, GTK_ALIGN_START)
            gtk_label_set_xalign(left, 0.0F)
            gtk_box_pack_start(row, left, False, False, 0)
            If value IsNot Nothing Then
                Dim right = gtk_label_new(value)
                AddCssClass(right, "dim")
                gtk_label_set_selectable(right, True)
                gtk_widget_set_halign(right, GTK_ALIGN_END)
                gtk_box_pack_end(row, right, False, False, 0)
            End If
            gtk_box_pack_start(box, row, False, False, 0)
            Return row
        End Function

        Private Function PageAppearance() As IntPtr
            Dim box = MakePane("Appearance")
            Dim raw = ReadFileOr("/run/dev-desktop/status", "")
            Dim live = raw.Contains("blur=live")
            Dim contrast = raw.Contains("contrast=1")

            Dim glass = MakeCard()
            Dim row = AddRow(glass, "Liquid Glass", Nothing)
            Dim sw = gtk_switch_new()
            gtk_switch_set_active(sw, Not contrast)
            SignalConnectSwitch(sw, Function(w, st, d)
                                        NativeBridge.olympium_send_compositor_cmd("contrast toggle" & vbLf)
                                        Return False
                                    End Function)
            gtk_box_pack_end(row, sw, False, False, 0)
            gtk_box_pack_start(box, glass, False, False, 0)
            If contrast Then
                AddNote(box, "Contrast is on, so surfaces are opaque. It also turns on by itself when the display cannot be read back.")
            ElseIf Not live Then
                AddNote(box, "Glass is the default. This session is still starting the live blur.")
            Else
                AddNote(box, "The menu bar, menus, dock, window title bars, dialogs, and Launchpad bend and blur the live picture behind them.")
            End If

            Dim typeCard = MakeCard()
            AddRow(typeCard, "System font", "Ubuntu")
            AddRow(typeCard, "Monospace", "Ubuntu Mono")
            gtk_box_pack_start(box, typeCard, False, False, 0)
            AddNote(box, "Ubuntu is the interface font. Scripts it does not cover fall back to Noto Sans, then Noto Sans CJK, then DejaVu Sans.")
            Return box
        End Function

        Private Function PageDrivers() As IntPtr
            Dim box = MakePane("Drivers")
            AddNote(box, "These driver and firmware packages are in this image. NVIDIA's proprietary driver and Broadcom STA are not included. In Terminal, run driver-install --detect.")
            Dim manifest = ReadFileOr("/usr/share/olympium/driver-manifest.txt", "The driver list is not available on this system.")
            Dim tv = gtk_text_view_new()
            gtk_text_view_set_editable(tv, False)
            gtk_text_view_set_monospace(tv, True)
            gtk_text_view_set_left_margin(tv, 14)
            gtk_text_view_set_right_margin(tv, 14)
            gtk_text_view_set_top_margin(tv, 10)
            gtk_text_view_set_bottom_margin(tv, 10)
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(tv), manifest, -1)
            Dim scroll = gtk_scrolled_window_new(IntPtr.Zero, IntPtr.Zero)
            gtk_scrolled_window_set_policy(scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC)
            gtk_scrolled_window_set_min_content_height(scroll, 280)
            AddCssClass(scroll, "card")
            gtk_container_add(scroll, tv)
            gtk_widget_set_margin_top(scroll, 4)
            gtk_box_pack_start(box, scroll, True, True, 0)
            Return box
        End Function

        Private Function PageCompat() As IntPtr
            Dim box = MakePane("Compatibility & Interop")
            Dim info = MakeCard()
            AddText(info,
                "Universal Cross-Architecture: x86 (32-bit), x86_64 (64-bit), ARM32 (armhf), and ARM64 (aarch64) software run seamlessly on any host CPU via native compat or qemu-user-static + binfmt_misc." & vbLf & vbLf &
                "Double-Click Native Install: Double-clicking any .exe, .msi, .deb, .rpm, .dmg, .pkg, .app, .apk, .hap, or cross-arch ELF installs and launches it in one step." & vbLf & vbLf &
                "Cross-App Interoperability: Shared Clipboard, Shared Folder (~/OlympiumShared <-> Windows S:\ <-> Android /sdcard/OlympiumShared), URI/File handoff, and Android AppWidget desktop integration keep every app feeling native." & vbLf & vbLf &
                "Windows (.exe / .msi): auto-Wine + binfmt_misc + prebuilt prefix. macOS (Intel / Universal2 / M-series): Darling GUI. Android (.apk) & OpenHarmony (.hap): slim scrcpy/adb + open-source microG Core (Apache-2.0) + local OpenHarmony SDK.")
            gtk_box_pack_start(box, info, False, False, 0)

            ' Cross-app interop switches (Shared Clipboard + Cross-Platform Widgets)
            Dim interopCard = MakeCard()
            Dim clipRow = AddRow(interopCard, "Shared Clipboard (Host <-> Wine <-> Android <-> OpenHarmony)", Nothing)
            Dim clipSw = gtk_switch_new()
            gtk_switch_set_active(clipSw, InteropHub.GetBoolSetting("clipboard_sync_enabled", True))
            SignalConnectSwitch(clipSw, Function(w, st, d)
                                            InteropHub.SetBoolSetting("clipboard_sync_enabled", st)
                                            If st Then InteropHub.SyncClipboardAll()
                                            Return False
                                        End Function)
            gtk_box_pack_end(clipRow, clipSw, False, False, 0)

            Dim widgetRow = AddRow(interopCard, "Cross-Platform Desktop Widgets (Android AppWidgets + Native)", Nothing)
            Dim widgetSw = gtk_switch_new()
            gtk_switch_set_active(widgetSw, InteropHub.GetBoolSetting("android_widgets_enabled", True))
            SignalConnectSwitch(widgetSw, Function(w, st, d)
                                              InteropHub.SetBoolSetting("android_widgets_enabled", st)
                                              If st Then InteropHub.SyncCrossPlatformWidgets()
                                              Return False
                                          End Function)
            gtk_box_pack_end(widgetRow, widgetSw, False, False, 0)
            gtk_box_pack_start(box, interopCard, False, False, 0)

            ' Built-in microG Core (Apache-2.0) + Dismissible Google Play Manual Setup Guide
            Dim microgCard = MakeCard()
            Dim mgStatus = InteropHub.MicroGStatus()
            Dim mgReady = CBool(mgStatus("microg_gmscore_bundled"))
            AddRow(microgCard, "Android microG Services Core (Apache-2.0)", If(mgReady, "Built-in (No Play Store)", "Auto-provisioned on Android launch"))
            Dim playRow = AddRow(microgCard, "Show Google Play manual setup guide", Nothing)
            Dim playSw = gtk_switch_new()
            Dim showGuide = InteropHub.GetBoolSetting("show_google_play_guide", True)
            gtk_switch_set_active(playSw, showGuide)
            gtk_box_pack_end(playRow, playSw, False, False, 0)
            gtk_box_pack_start(box, microgCard, False, False, 0)

            Dim guideCard = MakeCard()
            AddText(guideCard, InteropHub.GooglePlayGuideText())
            gtk_box_pack_start(box, guideCard, False, False, 0)
            If Not showGuide Then
                gtk_widget_set_no_show_all(guideCard, True)
                gtk_widget_hide(guideCard)
            End If
            SignalConnectSwitch(playSw, Function(w, st, d)
                                            InteropHub.SetBoolSetting("show_google_play_guide", st)
                                            gtk_widget_set_no_show_all(guideCard, Not st)
                                            If st Then
                                                gtk_widget_show_all(guideCard)
                                            Else
                                                gtk_widget_hide(guideCard)
                                            End If
                                            Return False
                                        End Function)
            Return box
        End Function

        Private Function PageOlympium() As IntPtr
            Dim box = MakePane("Olympium")
            Dim info = MakeCard()
            AddText(info,
                "If olympium-linux or linkspan-linux is installed, the dock entry starts it. " &
                "This image does not vendor that source and does not write back to it." & vbLf & vbLf &
                "Olympium desktop applications and multi-platform runtime managers are built on VB.NET (.NET 8) with a native Wayland compositor." & vbLf & vbLf &
                "iOS is not part of this system.")
            gtk_box_pack_start(box, info, False, False, 0)
            Return box
        End Function

        Private Function PageLicense() As IntPtr
            Dim box = MakePane("License")
            Dim info = MakeCard()
            Dim active = File.Exists("/var/lib/olympium/license")
            AddRow(info, "Purchase reminder", If(active, "Hidden", "Every 5 days"))
            gtk_box_pack_start(box, info, False, False, 0)
            AddNote(box, "Olympium is a commercial system. For the first 5 days there is no reminder. After that a reminder asks you to purchase a license, and you can always close it. Closing it changes nothing else. Run olympium-activate with your key in Terminal.")
            Return box
        End Function

        Private Function PageAbout() As IntPtr
            Dim box = MakePane("About")
            Dim info = MakeCard()
            Dim branding = ReadFileOr("/etc/dev-desktop/branding.conf", "")
            Dim version = "0.1.0"
            For Each line In branding.Split(vbLf)
                If line.StartsWith("version=") Then
                    version = line.Substring(8).Trim()
                End If
            Next
            AddRow(info, "Name", "Olympium")
            AddRow(info, "Version", version)
            AddRow(info, "Processor", UnameMachine())
            AddRow(info, "Kernel", UnameRelease())
            Dim browser = ReadFileOr("/usr/share/olympium/firefox-version.txt", "Firefox").Trim()
            If String.IsNullOrEmpty(browser) Then browser = "Firefox"
            AddRow(info, "Browser", browser)
            AddRow(info, "Desktop Runtime", "VB.NET (.NET 8) + Wayland")
            gtk_box_pack_start(box, info, False, False, 0)
            AddNote(box, "Olympium is a native Linux system with its own desktop. It is not macOS and does not contain Apple software.")
            Return box
        End Function

        Private Function SecretEntry(hint As String) As IntPtr
            Dim entry = gtk_entry_new()
            gtk_entry_set_visibility(entry, False)
            gtk_entry_set_input_purpose(entry, GTK_INPUT_PURPOSE_PASSWORD)
            gtk_entry_set_placeholder_text(entry, hint)
            gtk_widget_set_margin_start(entry, 16)
            gtk_widget_set_margin_end(entry, 16)
            gtk_widget_set_margin_top(entry, 6)
            gtk_widget_set_margin_bottom(entry, 6)
            Return entry
        End Function

        Private Function PlainEntry(hint As String) As IntPtr
            Dim entry = SecretEntry(hint)
            gtk_entry_set_visibility(entry, True)
            gtk_entry_set_input_purpose(entry, GTK_INPUT_PURPOSE_FREE_FORM)
            Return entry
        End Function

        Private Sub AddChangeForm(box As IntPtr, account As String, title As String)
            Dim group = MakeCard()
            AddText(group, title, "rowname")
            Dim hasPw = NativeBridge.olympium_acct_has_password(account) <> 0
            Dim cur = SecretEntry(If(hasPw, "Current password", "No password is set yet; leave this empty"))
            Dim fresh = SecretEntry("New password, at least 8 characters")
            Dim again = SecretEntry("New password again")
            gtk_box_pack_start(group, cur, False, False, 0)
            gtk_box_pack_start(group, fresh, False, False, 0)
            gtk_box_pack_start(group, again, False, False, 0)
            Dim btn = gtk_button_new_with_label("Change Password")
            gtk_widget_set_halign(btn, GTK_ALIGN_START)
            gtk_widget_set_margin_start(btn, 16)
            gtk_widget_set_margin_bottom(btn, 8)
            Dim result = gtk_label_new("")
            gtk_label_set_xalign(result, 0.0F)
            gtk_label_set_line_wrap(result, True)
            gtk_widget_set_margin_start(result, 16)
            gtk_widget_set_margin_bottom(result, 10)
            SignalConnect(btn, "clicked", Sub(w, d)
                                              Dim cText = PtrToUtf8(gtk_entry_get_text(cur))
                                              Dim fText = PtrToUtf8(gtk_entry_get_text(fresh))
                                              Dim aText = PtrToUtf8(gtk_entry_get_text(again))
                                              Dim whyBuf As New StringBuilder(200)
                                              Dim msg As String
                                              If NativeBridge.olympium_acct_has_password(account) <> 0 AndAlso
                                                 NativeBridge.olympium_acct_check_password(account, cText) = 0 Then
                                                  msg = "The current password is not right. Nothing was changed."
                                              ElseIf fText <> aText Then
                                                  msg = "The two new passwords are not the same. Nothing was changed."
                                              ElseIf NativeBridge.olympium_acct_password_ok(fText, whyBuf, New UIntPtr(200UI)) = 0 Then
                                                  msg = whyBuf.ToString()
                                              ElseIf NativeBridge.olympium_acct_set_password(account, fText) <> 0 Then
                                                  msg = "The password could not be changed. Changing it needs administrator rights."
                                              Else
                                                  msg = $"The password for {account} was changed."
                                                  gtk_entry_set_text(cur, "")
                                                  gtk_entry_set_text(fresh, "")
                                                  gtk_entry_set_text(again, "")
                                              End If
                                              gtk_label_set_text(result, msg)
                                          End Sub)
            gtk_box_pack_start(group, btn, False, False, 0)
            gtk_box_pack_start(group, result, False, False, 0)
            gtk_box_pack_start(box, group, False, False, 0)
        End Sub

        Private Function PageUsers() As IntPtr
            Dim box = MakePane("Users & Passwords")
            Dim primaryBuf As New StringBuilder(64)
            If NativeBridge.olympium_acct_primary(primaryBuf, New UIntPtr(64UI)) = 0 Then
                Dim acct = primaryBuf.ToString()
                AddChangeForm(box, acct, "Your account: " & acct)
            Else
                Dim group = MakeCard()
                AddText(group, "No account has been set up yet. First-launch setup makes one.")
                gtk_box_pack_start(box, group, False, False, 0)
            End If
            AddChangeForm(box, "root", "Administrator (root)")
            AddNote(box, "A password is checked against the one stored on this computer and is given to the system's own chpasswd. It is not written anywhere else.")
            Return box
        End Function

        Private Function RunFirstLaunch() As Integer
            Dim win = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_decorated(win, False)
            gtk_window_set_default_size(win, 560, 520)
            gtk_window_set_title(win, "Welcome to Olympium")
            SignalConnect(win, "destroy", Sub(w, d) gtk_main_quit())

            Dim box = MakePane("Welcome to Olympium")
            gtk_container_add(win, box)

            Dim who = MakeCard()
            AddText(who, "Your account", "rowname")
            Dim fullE = PlainEntry("Full name")
            Dim nameE = PlainEntry("Account name, such as dana")
            Dim passE = SecretEntry("Password, at least 8 characters")
            Dim pass2E = SecretEntry("Password again")
            gtk_box_pack_start(who, fullE, False, False, 0)
            gtk_box_pack_start(who, nameE, False, False, 0)
            gtk_box_pack_start(who, passE, False, False, 0)
            gtk_box_pack_start(who, pass2E, False, False, 0)
            gtk_box_pack_start(box, who, False, False, 0)

            Dim admin = MakeCard()
            AddText(admin, "Administrator (root) password", "rowname")
            Dim rootE = SecretEntry("Administrator password, at least 8 characters")
            Dim root2E = SecretEntry("Administrator password again")
            gtk_box_pack_start(admin, rootE, False, False, 0)
            gtk_box_pack_start(admin, root2E, False, False, 0)
            gtk_box_pack_start(box, admin, False, False, 0)
            AddNote(box, "Both can be changed later in Settings, under Users & Passwords.")

            Dim result = gtk_label_new("")
            gtk_label_set_xalign(result, 0.0F)
            gtk_label_set_line_wrap(result, True)
            gtk_label_set_max_width_chars(result, 56)
            gtk_box_pack_start(box, result, False, False, 0)

            Dim doSubmit As Action = Sub()
                                         Dim name = PtrToUtf8(gtk_entry_get_text(nameE))
                                         Dim full = PtrToUtf8(gtk_entry_get_text(fullE))
                                         Dim pass = PtrToUtf8(gtk_entry_get_text(passE))
                                         Dim pass2 = PtrToUtf8(gtk_entry_get_text(pass2E))
                                         Dim root = PtrToUtf8(gtk_entry_get_text(rootE))
                                         Dim root2 = PtrToUtf8(gtk_entry_get_text(root2E))
                                         Dim whyBuf As New StringBuilder(200)
                                         Dim msg As String
                                         If NativeBridge.olympium_acct_name_ok(name) = 0 Then
                                             msg = "The account name starts with a lower-case letter and has only lower-case letters, digits, - and _. It cannot be root."
                                         ElseIf pass <> pass2 Then
                                             msg = "The two account passwords are not the same."
                                         ElseIf NativeBridge.olympium_acct_password_ok(pass, whyBuf, New UIntPtr(200UI)) = 0 Then
                                             msg = whyBuf.ToString()
                                         ElseIf root <> root2 Then
                                             msg = "The two administrator passwords are not the same."
                                         ElseIf NativeBridge.olympium_acct_password_ok(root, whyBuf, New UIntPtr(200UI)) = 0 Then
                                             msg = whyBuf.ToString()
                                         ElseIf NativeBridge.olympium_acct_create(name, full, pass) <> 0 Then
                                             msg = "The account could not be created."
                                         ElseIf NativeBridge.olympium_acct_set_password("root", root) <> 0 Then
                                             msg = "The administrator password could not be set."
                                         Else
                                             NativeBridge.olympium_acct_mark_setup_done(name)
                                             Console.WriteLine($"setup: account {name} created, administrator password set")
                                             Console.Out.Flush()
                                             gtk_widget_destroy(win)
                                             Return
                                         End If
                                         gtk_label_set_text(result, msg)
                                     End Sub

            Dim go = gtk_button_new_with_label("Create Account")
            gtk_widget_set_halign(go, GTK_ALIGN_END)
            SignalConnect(go, "clicked", Sub(w, d) doSubmit())
            gtk_box_pack_start(box, go, False, False, 0)
            gtk_widget_show_all(win)
            gtk_widget_grab_focus(fullE)

            Dim autoSpec = Environment.GetEnvironmentVariable("OLYMPIUM_SETUP_AUTO")
            If Not String.IsNullOrEmpty(autoSpec) Then
                Dim parts = autoSpec.Split("|"c)
                If parts.Length >= 4 Then
                    g_timeout_add(300UI, PinDelegate(New TimeoutCallback(Function(d)
                                                                             gtk_entry_set_text(fullE, parts(0))
                                                                             gtk_entry_set_text(nameE, parts(1))
                                                                             gtk_entry_set_text(passE, parts(2))
                                                                             gtk_entry_set_text(pass2E, parts(2))
                                                                             gtk_entry_set_text(rootE, parts(3))
                                                                             gtk_entry_set_text(root2E, parts(3))
                                                                             doSubmit()
                                                                             Return False
                                                                         End Function)), IntPtr.Zero)
                End If
            End If
            gtk_main()
            Return If(NativeBridge.olympium_acct_setup_done() <> 0, 0, 1)
        End Function

        Private Sub SelectPane(id As String)
            If gtk_stack_get_child_by_name(_stack, id) = IntPtr.Zero Then Return
            gtk_stack_set_visible_child_name(_stack, id)
            For Each item In _navItems
                Dim ctx = gtk_widget_get_style_context(item.Button)
                If item.Id = id Then
                    gtk_style_context_add_class(ctx, "active")
                Else
                    gtk_style_context_remove_class(ctx, "active")
                End If
            Next
        End Sub

        Private Sub AddPane(side As IntPtr, id As String, label As String, page As IntPtr)
            gtk_stack_add_named(_stack, page, id)
            Dim btn = gtk_button_new()
            gtk_button_set_relief(btn, GTK_RELIEF_NONE)
            AddCssClass(btn, "place")
            Dim row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8)
            Dim icon = gtk_drawing_area_new()
            gtk_widget_set_size_request(icon, 28, 28)
            Dim capturedId = id
            SignalConnectDraw(icon, Function(w, cr, d) DrawNavIcon(capturedId, cr))
            Dim text = gtk_label_new(label)
            gtk_label_set_xalign(text, 0.0F)
            gtk_widget_set_halign(text, GTK_ALIGN_START)
            gtk_box_pack_start(row, icon, False, False, 0)
            gtk_box_pack_start(row, text, True, True, 0)
            gtk_container_add(btn, row)
            SignalConnect(btn, "clicked", Sub(w, d) SelectPane(capturedId))
            gtk_box_pack_start(side, btn, False, False, 0)
            _navItems.Add((id, label, btn))
        End Sub

        Public Function Run(args As String()) As Integer
            Dim argc = 0
            Dim argv = IntPtr.Zero
            gtk_init(argc, argv)
            g_set_prgname("dev-settings")
            g_object_set(gtk_settings_get_default(), "gtk-font-name", "Ubuntu 12", IntPtr.Zero)
            LoadCss(
                "window { background: #f2f2f6; }" &
                "label { color: #1d1d1f; }" &
                ".sidebar { background: #e7e7ec; }" &
                "entry { background: rgba(120,120,128,0.16); color: #1d1d1f; border: none; border-radius: 8px; box-shadow: none; padding: 3px 8px; min-height: 26px; caret-color: #0a84ff; }" &
                "button.place { padding: 4px 8px; margin: 1px 10px; border-radius: 8px; border: none; background: none; box-shadow: none; }" &
                "button.place:hover { background: rgba(0,0,0,0.05); }" &
                "button.place.active, button.place.active:hover { background: rgba(0,0,0,0.10); }" &
                ".heading { font-weight: bold; font-size: 28px; }" &
                ".dim { color: #8e8e93; }" &
                ".note { color: #8e8e93; font-size: 12px; }" &
                ".card { background: #ffffff; border: none; border-radius: 12px; box-shadow: none; padding: 0; }" &
                "separator.rowsep { background: #e5e5ea; min-height: 1px; }" &
                "textview, textview text { background: #ffffff; color: #1d1d1f; font-family: 'Ubuntu Mono', monospace; font-size: 12px; }" &
                "switch { background: #e9e9eb; border: none; border-radius: 12px; min-width: 42px; min-height: 24px; background-image: none; box-shadow: inset 0 0 0 1px rgba(0,0,0,0.05); }" &
                "switch:checked { background: #34c759; }" &
                "switch slider { background: #ffffff; border: none; border-radius: 10px; min-width: 20px; min-height: 20px; box-shadow: 0 1px 2px rgba(0,0,0,0.28); }")

            Dim page = "appearance"
            For Each a In args
                If a = "--first-run" Then Return RunFirstLaunch()
                If a.StartsWith("--page=") Then page = a.Substring(7)
            Next
            If page = "compat" Then page = "compatibility"
            If page = "desktop" Then page = "appearance"

            Dim win = gtk_window_new(GTK_WINDOW_TOPLEVEL)
            gtk_window_set_decorated(win, False)
            gtk_window_set_default_size(win, 900, 560)
            gtk_window_set_title(win, "Settings")
            SignalConnect(win, "destroy", Sub(w, d) gtk_main_quit())

            Dim root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)
            gtk_container_add(win, root)

            Dim side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2)
            AddCssClass(side, "sidebar")
            gtk_widget_set_size_request(side, 236, -1)
            Dim search = gtk_search_entry_new()
            gtk_entry_set_placeholder_text(search, "Search")
            gtk_widget_set_margin_top(search, 14)
            gtk_widget_set_margin_start(search, 12)
            gtk_widget_set_margin_end(search, 12)
            gtk_widget_set_margin_bottom(search, 8)
            gtk_box_pack_start(side, search, False, False, 0)
            SignalConnect(search, "changed", Sub(w, d)
                                                 Dim query = PtrToUtf8(gtk_entry_get_text(search))
                                                 Dim shown = 0
                                                 Dim firstId As String = Nothing
                                                 Dim activeHidden = False
                                                 Dim activeId As String = Nothing
                                                 For Each item In _navItems
                                                     Dim matched = String.IsNullOrEmpty(query) OrElse item.Label.Contains(query, StringComparison.OrdinalIgnoreCase)
                                                     gtk_widget_set_visible(item.Button, matched)
                                                     If matched Then
                                                         shown += 1
                                                         If firstId Is Nothing Then firstId = item.Id
                                                     End If
                                                     If gtk_style_context_has_class(gtk_widget_get_style_context(item.Button), "active") Then
                                                         activeId = item.Id
                                                         If Not matched Then activeHidden = True
                                                     End If
                                                 Next
                                                 gtk_widget_set_visible(_emptyLabel, shown = 0)
                                                 If activeHidden AndAlso firstId IsNot Nothing Then
                                                     SelectPane(firstId)
                                                 ElseIf activeId IsNot Nothing AndAlso String.IsNullOrEmpty(query) Then
                                                     SelectPane(activeId)
                                                 End If
                                             End Sub)

            Dim pad = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1)
            gtk_box_pack_start(side, pad, False, False, 0)
            _emptyLabel = gtk_label_new("No Results")
            AddCssClass(_emptyLabel, "note")
            gtk_widget_set_margin_top(_emptyLabel, 18)
            gtk_widget_set_no_show_all(_emptyLabel, True)
            gtk_widget_hide(_emptyLabel)
            gtk_box_pack_start(side, _emptyLabel, False, False, 0)
            gtk_box_pack_start(root, side, False, False, 0)

            Dim scroll = gtk_scrolled_window_new(IntPtr.Zero, IntPtr.Zero)
            gtk_scrolled_window_set_policy(scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC)
            gtk_box_pack_start(root, scroll, True, True, 0)
            _stack = gtk_stack_new()
            gtk_container_add(scroll, _stack)

            AddPane(pad, "appearance", "Appearance", PageAppearance())
            AddPane(pad, "users", "Users & Passwords", PageUsers())
            AddPane(pad, "drivers", "Drivers", PageDrivers())
            AddPane(pad, "compatibility", "Compatibility", PageCompat())
            AddPane(pad, "olympium", "Olympium", PageOlympium())
            AddPane(pad, "license", "License", PageLicense())
            AddPane(pad, "about", "About", PageAbout())

            gtk_widget_show_all(win)
            SelectPane(page)
            gtk_main()
            Return 0
        End Function
    End Module

End Namespace
