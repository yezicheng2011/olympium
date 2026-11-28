/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * A tiny program used to show which CPU a process is actually running on.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	const char *mach =
#if defined(__x86_64__)
		"x86_64";
#elif defined(__i386__)
		"i386";
#elif defined(__aarch64__)
		"aarch64";
#elif defined(__arm__)
		"arm";
#else
		"unknown";
#endif
	const char *msg =
#ifdef MSG
		MSG;
#else
		"hello";
#endif
	printf("%s bits=%d machine=%s\n", msg, (int)(sizeof(void *) * 8), mach);
	fflush(stdout);
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--hold") == 0) {
			puts("holding");
			fflush(stdout);
			sleep(90);
		}
	}
	return 0;
}
