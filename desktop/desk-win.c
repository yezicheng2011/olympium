/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * A small Windows program built with mingw. Wine runs it. It is not a
 * Microsoft application and it does not include Windows.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD g_start;

static void write_text(const char *name, const char *text) {
	const char *home = getenv("OLYMPIUM_DESK_HOME");
	if (home == NULL || home[0] == '\0') {
		home = "C:\\";
	}
	char path[520];
	snprintf(path, sizeof path, "%s/%s", home, name);
	char *slash = strrchr(path, '/');
	if (slash != NULL) {
		*slash = '\0';
		CreateDirectoryA(path, NULL);
		*slash = '/';
	}
	FILE *f = fopen(path, "w");
	if (f != NULL) {
		fputs(text, f);
		fclose(f);
	}
}

static void remember_ready(void) {
	char line[64];
	snprintf(line, sizeof line, "%lu\n", (unsigned long)(GetTickCount() - g_start));
	const char *home = getenv("OLYMPIUM_DESK_HOME");
	const char *which = "wine-cold-ms";
	if (home != NULL && home[0] != '\0') {
		char cold[520];
		snprintf(cold, sizeof cold, "%s/wine-cold-ms", home);
		if (GetFileAttributesA(cold) != INVALID_FILE_ATTRIBUTES) {
			which = "wine-warm-ms";
		}
	}
	write_text(which, line);
	write_text("exchange/desk-win.txt", "Desk Win ready\n");
}

static void claim_clipboard(HWND hwnd) {
	if (!OpenClipboard(hwnd)) {
		return;
	}
	const char *text = "Desk Win ready";
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, strlen(text) + 1);
	if (mem != NULL) {
		char *dst = GlobalLock(mem);
		if (dst != NULL) {
			memcpy(dst, text, strlen(text) + 1);
			GlobalUnlock(mem);
			EmptyClipboard();
			SetClipboardData(CF_TEXT, mem);
		}
	}
	CloseClipboard();
}

/* --paste: read the text another program put on the clipboard and record it,
 * so a test can see that a Wayland copy reached a Windows program. */
static void read_clipboard(HWND hwnd) {
	char text[256] = "empty";
	if (OpenClipboard(hwnd)) {
		HANDLE mem = GetClipboardData(CF_TEXT);
		if (mem != NULL) {
			const char *src = GlobalLock(mem);
			if (src != NULL) {
				snprintf(text, sizeof text, "%s", src);
				GlobalUnlock(mem);
			}
		}
		CloseClipboard();
	}
	char line[300];
	snprintf(line, sizeof line, "%s\n", text);
	write_text("exchange/desk-win-paste.txt", line);
}

static int g_paste;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	switch (msg) {
	case WM_CREATE:
		CreateDirectoryA("exchange", NULL);
		remember_ready();
		if (!g_paste) {
			claim_clipboard(hwnd);
			/* That copy happens before the window is on screen, and the
			 * desktop does not let an X program that is not in front
			 * replace the clipboard. Copy again shortly after, which is
			 * when a person could first press Ctrl+C here. */
			SetTimer(hwnd, 3, 600, NULL);
		}
		return 0;
	case WM_SETFOCUS:
		if (g_paste) {
			/* The X selection is offered to the window that has focus. */
			SetTimer(hwnd, 2, 300, NULL);
		}
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		TextOutA(dc, 24, 48, "Desk Win ready", 14);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_TIMER:
		if (wparam == 2) {
			KillTimer(hwnd, 2);
			read_clipboard(hwnd);
			return 0;
		}
		if (wparam == 3) {
			KillTimer(hwnd, 3);
			claim_clipboard(hwnd);
			return 0;
		}
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show) {
	(void)prev;
	(void)show;
	g_start = GetTickCount();
	int once = cmd != NULL && strstr(cmd, "--once") != NULL;
	g_paste = cmd != NULL && strstr(cmd, "--paste") != NULL;
	WNDCLASSA cls;
	memset(&cls, 0, sizeof cls);
	cls.lpfnWndProc = wndproc;
	cls.hInstance = instance;
	cls.lpszClassName = "DeskWin";
	cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	cls.hCursor = LoadCursor(NULL, IDC_ARROW);
	RegisterClassA(&cls);
	HWND hwnd = CreateWindowExA(0, "DeskWin", "Desk Win",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		120, 80, 480, 280, NULL, NULL, instance, NULL);
	if (hwnd == NULL) {
		return 1;
	}
	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	if (once) {
		SetTimer(hwnd, 1, 800, NULL);
	}
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}
