/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Does the one thing a Windows installer does that matters to Launchpad: it
 * puts a shortcut in the Start Menu. Built with mingw and run under Wine by
 * the test. Usage: desk-lnk.exe TARGET.EXE "Shortcut Name"
 */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: desk-lnk TARGET NAME\n");
		return 2;
	}
	char programs[MAX_PATH];
	if (FAILED(CoInitialize(NULL)) ||
			FAILED(SHGetFolderPathA(NULL, CSIDL_PROGRAMS | CSIDL_FLAG_CREATE, NULL, 0, programs))) {
		fprintf(stderr, "desk-lnk: no Start Menu folder\n");
		return 1;
	}
	IShellLinkA *link = NULL;
	if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			&IID_IShellLinkA, (void **)&link))) {
		fprintf(stderr, "desk-lnk: no shell link object\n");
		return 1;
	}
	IShellLinkA_SetPath(link, argv[1]);
	IShellLinkA_SetDescription(link, argv[2]);
	if (argc > 3) {
		IShellLinkA_SetArguments(link, argv[3]);
	}
	IPersistFile *file = NULL;
	int rc = 1;
	if (SUCCEEDED(IShellLinkA_QueryInterface(link, &IID_IPersistFile, (void **)&file))) {
		char path[MAX_PATH];
		WCHAR wide[MAX_PATH];
		snprintf(path, sizeof path, "%s\\%s.lnk", programs, argv[2]);
		MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH);
		if (SUCCEEDED(IPersistFile_Save(file, wide, TRUE))) {
			printf("shortcut %s\n", path);
			rc = 0;
		}
		IPersistFile_Release(file);
	}
	IShellLinkA_Release(link);
	CoUninitialize();
	return rc;
}
