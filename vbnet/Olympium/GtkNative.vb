' Copyright 2026 Development Desktop contributors
' SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
'
' Minimal zero-dependency GTK3 / GDK / Cairo / GdkPixbuf / GLib P/Invoke layer
' so all Olympium desktop GUI apps (Settings, Files, Install, Swatch, Test App)
' run natively in VB.NET (.NET 8) against the system's libgtk-3.so.0.

Imports System
Imports System.Collections.Generic
Imports System.Runtime.InteropServices

Namespace Olympium

    Public Module GtkNative
        Public Const LibGtk As String = "libgtk-3.so.0"
        Public Const LibGdk As String = "libgdk-3.so.0"
        Public Const LibGobj As String = "libgobject-2.0.so.0"
        Public Const LibGlib As String = "libglib-2.0.so.0"
        Public Const LibCairo As String = "libcairo.so.2"
        Public Const LibPixbuf As String = "libgdk_pixbuf-2.0.so.0"

        ' Keep managed delegates alive for the lifetime of GTK callbacks
        Private ReadOnly AliveDelegates As New List(Of [Delegate])()

        Public Function PinDelegate(d As [Delegate]) As IntPtr
            AliveDelegates.Add(d)
            Return Marshal.GetFunctionPointerForDelegate(d)
        End Function

        <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
        Public Delegate Sub SimpleCallback(widget As IntPtr, userData As IntPtr)

        <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
        Public Delegate Function DrawCallback(widget As IntPtr, cr As IntPtr, userData As IntPtr) As Boolean

        <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
        Public Delegate Function SwitchCallback(sw As IntPtr, state As Boolean, userData As IntPtr) As Boolean

        <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
        Public Delegate Function TimeoutCallback(userData As IntPtr) As Boolean

        <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
        Public Delegate Sub TreeRowActivatedCallback(treeView As IntPtr, path As IntPtr, col As IntPtr, userData As IntPtr)

        ' ---- Constants ----
        Public Const GTK_WINDOW_TOPLEVEL As Integer = 0
        Public Const GTK_ORIENTATION_HORIZONTAL As Integer = 0
        Public Const GTK_ORIENTATION_VERTICAL As Integer = 1
        Public Const GTK_ALIGN_FILL As Integer = 0
        Public Const GTK_ALIGN_START As Integer = 1
        Public Const GTK_ALIGN_END As Integer = 2
        Public Const GTK_ALIGN_CENTER As Integer = 3
        Public Const GTK_RELIEF_NONE As Integer = 2
        Public Const GTK_POLICY_AUTOMATIC As Integer = 1
        Public Const GTK_POLICY_NEVER As Integer = 2
        Public Const GTK_SELECTION_NONE As Integer = 0
        Public Const GTK_STYLE_PROVIDER_PRIORITY_APPLICATION As UInteger = 600UI
        Public Const GTK_ICON_SIZE_DIALOG As Integer = 6
        Public Const GTK_INPUT_PURPOSE_FREE_FORM As Integer = 0
        Public Const GTK_INPUT_PURPOSE_PASSWORD As Integer = 7
        Public Const PANGO_ELLIPSIZE_MIDDLE As Integer = 2
        Public Const PANGO_ELLIPSIZE_END As Integer = 3
        Public Const CAIRO_LINE_CAP_ROUND As Integer = 1
        Public Const CAIRO_LINE_JOIN_ROUND As Integer = 1
        Public Const CAIRO_FONT_SLANT_NORMAL As Integer = 0
        Public Const CAIRO_FONT_WEIGHT_NORMAL As Integer = 0
        Public Const CAIRO_FONT_WEIGHT_BOLD As Integer = 1
        Public Const CAIRO_FILTER_BEST As Integer = 2

        ' ---- GTK Core ----
        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_init(ByRef argc As Integer, ByRef argv As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_init_check(argc As IntPtr, argv As IntPtr) As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_main()
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_main_quit()
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_events_pending() As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_main_iteration() As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_settings_get_default() As IntPtr
        End Function

        <DllImport(LibGlib, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub g_set_prgname(prgname As String)
        End Sub

        <DllImport(LibGlib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function g_timeout_add(interval As UInteger, cb As IntPtr, data As IntPtr) As UInteger
        End Function

        <DllImport(LibGlib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function g_idle_add(cb As IntPtr, data As IntPtr) As UInteger
        End Function

        <DllImport(LibGlib, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub g_free(mem As IntPtr)
        End Sub

        ' ---- GObject ----
        <DllImport(LibGobj, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function g_signal_connect_data(instance As IntPtr, detailedSignal As String, cHandler As IntPtr, data As IntPtr, destroyData As IntPtr, connectFlags As Integer) As ULong
        End Function

        <DllImport(LibGobj, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub g_object_set(obj As IntPtr, propName As String, val As String, terminator As IntPtr)
        End Sub

        <DllImport(LibGobj, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub g_object_unref(obj As IntPtr)
        End Sub

        <DllImport(LibGobj, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function g_type_from_name(name As String) As UIntPtr
        End Function

        Public Sub SignalConnect(widget As IntPtr, sig As String, cb As SimpleCallback, Optional data As IntPtr = Nothing)
            g_signal_connect_data(widget, sig, PinDelegate(cb), data, IntPtr.Zero, 0)
        End Sub

        Public Sub SignalConnectDraw(widget As IntPtr, cb As DrawCallback, Optional data As IntPtr = Nothing)
            g_signal_connect_data(widget, "draw", PinDelegate(cb), data, IntPtr.Zero, 0)
        End Sub

        Public Sub SignalConnectSwitch(widget As IntPtr, cb As SwitchCallback, Optional data As IntPtr = Nothing)
            g_signal_connect_data(widget, "state-set", PinDelegate(cb), data, IntPtr.Zero, 0)
        End Sub

        ' ---- CSS / Style ----
        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_css_provider_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_css_provider_load_from_data(provider As IntPtr, data As String, length As IntPtr, err As IntPtr) As Boolean
        End Function

        <DllImport(LibGdk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gdk_screen_get_default() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_style_context_add_provider_for_screen(screen As IntPtr, provider As IntPtr, priority As UInteger)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_widget_get_style_context(widget As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_style_context_add_class(ctx As IntPtr, cls As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_style_context_remove_class(ctx As IntPtr, cls As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_style_context_has_class(ctx As IntPtr, cls As String) As Boolean
        End Function

        Public Sub LoadCss(cssText As String)
            Dim provider = gtk_css_provider_new()
            gtk_css_provider_load_from_data(provider, cssText, New IntPtr(-1), IntPtr.Zero)
            gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION)
        End Sub

        Public Sub AddCssClass(widget As IntPtr, cls As String)
            gtk_style_context_add_class(gtk_widget_get_style_context(widget), cls)
        End Sub

        ' ---- Window & Widget ----
        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_window_new(winType As Integer) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_window_set_title(win As IntPtr, title As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_window_set_decorated(win As IntPtr, setting As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_window_set_default_size(win As IntPtr, width As Integer, height As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_window_set_resizable(win As IntPtr, resizable As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_show_all(widget As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_hide(widget As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_visible(widget As IntPtr, visible As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_no_show_all(widget As IntPtr, noShowAll As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_destroy(widget As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_sensitive(widget As IntPtr, sensitive As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_widget_is_sensitive(widget As IntPtr) As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_size_request(widget As IntPtr, w As Integer, h As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_halign(widget As IntPtr, align As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_valign(widget As IntPtr, align As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_margin_start(widget As IntPtr, margin As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_margin_end(widget As IntPtr, margin As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_margin_top(widget As IntPtr, margin As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_set_margin_bottom(widget As IntPtr, margin As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_grab_focus(widget As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_widget_queue_draw(widget As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_widget_get_allocated_width(widget As IntPtr) As Integer
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_widget_get_allocated_height(widget As IntPtr) As Integer
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_widget_get_window(widget As IntPtr) As IntPtr
        End Function

        ' ---- Containers ----
        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_container_add(container As IntPtr, widget As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_container_set_border_width(container As IntPtr, borderWidth As UInteger)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_container_get_children(container As IntPtr) As IntPtr
        End Function

        <DllImport(LibGlib, CallingConvention:=CallingConvention.Cdecl)>
        Public Function g_list_length(list As IntPtr) As UInteger
        End Function

        <DllImport(LibGlib, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub g_list_free(list As IntPtr)
        End Sub

        <StructLayout(LayoutKind.Sequential)>
        Public Structure GListNode
            Public Data As IntPtr
            Public [Next] As IntPtr
            Public Prev As IntPtr
        End Structure

        Public Sub ClearContainer(container As IntPtr)
            Dim head = gtk_container_get_children(container)
            Dim cur = head
            While cur <> IntPtr.Zero
                Dim node = Marshal.PtrToStructure(Of GListNode)(cur)
                If node.Data <> IntPtr.Zero Then
                    gtk_widget_destroy(node.Data)
                End If
                cur = node.Next
            End While
            If head <> IntPtr.Zero Then g_list_free(head)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_box_new(orientation As Integer, spacing As Integer) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_box_pack_start(box As IntPtr, child As IntPtr, expand As Boolean, fill As Boolean, padding As UInteger)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_box_pack_end(box As IntPtr, child As IntPtr, expand As Boolean, fill As Boolean, padding As UInteger)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_scrolled_window_new(hadj As IntPtr, vadj As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_scrolled_window_set_policy(sw As IntPtr, hpol As Integer, vpol As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_scrolled_window_set_min_content_height(sw As IntPtr, height As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_stack_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_stack_add_named(stack As IntPtr, child As IntPtr, name As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_stack_set_visible_child_name(stack As IntPtr, name As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_stack_get_child_by_name(stack As IntPtr, name As String) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_flow_box_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_flow_box_set_selection_mode(fb As IntPtr, mode As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_flow_box_set_max_children_per_line(fb As IntPtr, n As UInteger)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_flow_box_set_homogeneous(fb As IntPtr, homo As Boolean)
        End Sub

        ' ---- Controls ----
        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_label_new(str As String) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_label_set_text(label As IntPtr, str As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_label_get_text(label As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_label_set_markup(label As IntPtr, str As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_label_set_xalign(label As IntPtr, xalign As Single)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_label_set_line_wrap(label As IntPtr, wrap As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_label_set_max_width_chars(label As IntPtr, n As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_label_set_selectable(label As IntPtr, setting As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_label_set_ellipsize(label As IntPtr, mode As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_button_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_button_new_with_label(label As String) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_button_set_label(button As IntPtr, label As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_button_set_relief(button As IntPtr, relief As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_button_clicked(button As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_check_button_new_with_label(label As String) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_toggle_button_get_active(btn As IntPtr) As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_toggle_button_set_active(btn As IntPtr, active As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_switch_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_switch_set_active(sw As IntPtr, active As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_entry_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_search_entry_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_entry_get_text(entry As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_entry_set_text(entry As IntPtr, text As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_entry_set_placeholder_text(entry As IntPtr, text As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_entry_set_visibility(entry As IntPtr, visible As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_entry_set_input_purpose(entry As IntPtr, purpose As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_separator_new(orientation As Integer) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_drawing_area_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_progress_bar_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_progress_bar_set_fraction(bar As IntPtr, frac As Double)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_progress_bar_set_text(bar As IntPtr, text As String)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_progress_bar_set_show_text(bar As IntPtr, show As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_text_view_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_text_view_set_editable(tv As IntPtr, editable As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_text_view_set_monospace(tv As IntPtr, mono As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_text_view_set_left_margin(tv As IntPtr, m As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_text_view_set_right_margin(tv As IntPtr, m As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_text_view_set_top_margin(tv As IntPtr, m As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_text_view_set_bottom_margin(tv As IntPtr, m As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_text_view_get_buffer(tv As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_text_buffer_set_text(buf As IntPtr, text As String, len As Integer)
        End Sub

        ' ---- Images & Pixbuf ----
        <DllImport(LibPixbuf, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gdk_pixbuf_new_from_file_at_scale(filename As String, width As Integer, height As Integer, preserveAspect As Boolean, err As IntPtr) As IntPtr
        End Function

        <DllImport(LibGdk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gdk_pixbuf_get_from_window(win As IntPtr, srcX As Integer, srcY As Integer, width As Integer, height As Integer) As IntPtr
        End Function

        <DllImport(LibPixbuf, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gdk_pixbuf_save(pixbuf As IntPtr, filename As String, type As String, err As IntPtr, term As IntPtr) As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_image_new_from_pixbuf(pixbuf As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_image_new_from_icon_name(iconName As String, size As Integer) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_image_set_pixel_size(img As IntPtr, pixelSize As Integer)
        End Sub

        ' ---- ListStore & TreeView ----
        <StructLayout(LayoutKind.Sequential)>
        Public Structure GtkTreeIter
            Public Stamp As Integer
            Public UserData As IntPtr
            Public UserData2 As IntPtr
            Public UserData3 As IntPtr
        End Structure

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_list_store_newv(nColumns As Integer, types As UIntPtr()) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_list_store_clear(store As IntPtr)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_list_store_append(store As IntPtr, ByRef iter As GtkTreeIter)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub gtk_list_store_set(store As IntPtr, ByRef iter As GtkTreeIter,
                                      col0 As Integer, val0 As String,
                                      col1 As Integer, val1 As String,
                                      col2 As Integer, val2 As String,
                                      terminator As Integer)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_tree_view_new_with_model(model As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_cell_renderer_text_new() As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Function gtk_tree_view_insert_column_with_attributes(tv As IntPtr, position As Integer, title As String, cell As IntPtr, attr As String, col As Integer, terminator As IntPtr) As Integer
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_tree_view_get_column(tv As IntPtr, n As Integer) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_tree_view_column_set_expand(col As IntPtr, expand As Boolean)
        End Sub

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_tree_view_get_model(tv As IntPtr) As IntPtr
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Function gtk_tree_model_get_iter(model As IntPtr, ByRef iter As GtkTreeIter, path As IntPtr) As Boolean
        End Function

        <DllImport(LibGtk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gtk_tree_model_get(model As IntPtr, ByRef iter As GtkTreeIter, col0 As Integer, ByRef val0 As IntPtr, terminator As Integer)
        End Sub

        ' ---- Cairo ----
        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_save(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_restore(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_translate(cr As IntPtr, tx As Double, ty As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_scale(cr As IntPtr, sx As Double, sy As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_new_path(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_new_sub_path(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_close_path(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_arc(cr As IntPtr, xc As Double, yc As Double, radius As Double, a1 As Double, a2 As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_move_to(cr As IntPtr, x As Double, y As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_line_to(cr As IntPtr, x As Double, y As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_rectangle(cr As IntPtr, x As Double, y As Double, w As Double, h As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_source_rgb(cr As IntPtr, r As Double, g As Double, b As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_source_rgba(cr As IntPtr, r As Double, g As Double, b As Double, a As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_line_width(cr As IntPtr, w As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_line_cap(cr As IntPtr, cap As Integer)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_line_join(cr As IntPtr, join As Integer)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_fill(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_fill_preserve(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_stroke(cr As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Function cairo_pattern_create_linear(x0 As Double, y0 As Double, x1 As Double, y1 As Double) As IntPtr
        End Function

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_pattern_add_color_stop_rgb(pat As IntPtr, offset As Double, r As Double, g As Double, b As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_source(cr As IntPtr, source As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_pattern_destroy(pat As IntPtr)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub cairo_select_font_face(cr As IntPtr, family As String, slant As Integer, weight As Integer)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_set_font_size(cr As IntPtr, size As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl, CharSet:=CharSet.Ansi)>
        Public Sub cairo_show_text(cr As IntPtr, utf8 As String)
        End Sub

        <DllImport(LibGdk, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub gdk_cairo_set_source_pixbuf(cr As IntPtr, pixbuf As IntPtr, x As Double, y As Double)
        End Sub

        <DllImport(LibCairo, CallingConvention:=CallingConvention.Cdecl)>
        Public Sub cairo_paint(cr As IntPtr)
        End Sub

        Public Sub CairoRoundedRect(cr As IntPtr, x As Double, y As Double, w As Double, h As Double, r As Double)
            cairo_new_sub_path(cr)
            cairo_arc(cr, x + w - r, y + r, r, -Math.PI / 2.0, 0.0)
            cairo_arc(cr, x + w - r, y + h - r, r, 0.0, Math.PI / 2.0)
            cairo_arc(cr, x + r, y + h - r, r, Math.PI / 2.0, Math.PI)
            cairo_arc(cr, x + r, y + r, r, Math.PI, 1.5 * Math.PI)
            cairo_close_path(cr)
        End Sub

        Public Function MarkupEscape(s As String) As String
            If String.IsNullOrEmpty(s) Then Return ""
            Return s.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("""", "&quot;").Replace("'", "&apos;")
        End Function
    End Module

End Namespace
