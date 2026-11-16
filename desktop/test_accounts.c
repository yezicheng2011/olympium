/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
 *
 * Checks accounts.c against the root named by OLYMPIUM_ACCT_ROOT.
 *   test-accounts names                 the name and password rules
 *   test-accounts check USER PASSWORD   exit 0 when the password is right
 *   test-accounts set USER PASSWORD     change it
 */
#include "accounts.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	char why[128];
	if (argc == 2 && strcmp(argv[1], "names") == 0) {
		int ok = acct_name_ok("dana") && acct_name_ok("d2-x_y") && !acct_name_ok("root") &&
			!acct_name_ok("Dana") && !acct_name_ok("1dana") && !acct_name_ok("da na") &&
			!acct_name_ok("a:b") && !acct_name_ok("") &&
			!acct_password_ok("short", why, sizeof why) &&
			!acct_password_ok("line\nbreak-long", why, sizeof why) &&
			acct_password_ok("long enough:with colon", why, sizeof why);
		puts(ok ? "names ok" : "names FAILED");
		return ok ? 0 : 1;
	}
	if (argc == 4 && strcmp(argv[1], "check") == 0) {
		return acct_check_password(argv[2], argv[3]) ? 0 : 1;
	}
	if (argc == 4 && strcmp(argv[1], "set") == 0) {
		return acct_set_password(argv[2], argv[3]) == 0 ? 0 : 1;
	}
	return 2;
}
