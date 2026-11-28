/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Accounts and passwords for first-launch setup and for Settings. The work
 * is done by the system's own useradd, usermod and chpasswd. A password is
 * handed to chpasswd on its standard input and is never on a command line.
 * OLYMPIUM_ACCT_ROOT names another root to work in, for the test.
 */
#ifndef DEV_ACCOUNTS_H
#define DEV_ACCOUNTS_H

#include <stddef.h>

/* 1 when name can be a login name: a lower-case letter, then up to 31 of
 * lower-case letters, digits, '-' and '_', and not "root". */
int acct_name_ok(const char *name);

/* 1 when the password can be used. Otherwise why says what is wrong. */
int acct_password_ok(const char *password, char *why, size_t n);

/* 1 when the account exists. */
int acct_exists(const char *name);

/* 1 when the account has a password set, 0 when it is locked or empty. */
int acct_has_password(const char *name);

/* 1 when password is the account's password. */
int acct_check_password(const char *name, const char *password);

/* Sets the account's password. 0 on success. */
int acct_set_password(const char *name, const char *password);

/* Creates the account with a home folder, or updates its full name when it
 * is already there, and sets its password. 0 on success. */
int acct_create(const char *name, const char *full_name, const char *password);

/* The first-launch marker. */
int acct_setup_done(void);
int acct_mark_setup_done(const char *name);

/* The account first-launch setup made, or the first ordinary one. */
int acct_primary(char *out, size_t n);

#endif
