/*
 * Copyright 2026 Daniel Ye
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Confines a child process before it unpacks a file nobody has vetted.
 *
 * A disk image or an installer package is whatever its author wanted it to
 * be. The tools that read them (7-Zip, cpio) run as the session's user,
 * which on the live image is root, and both have had bugs where a crafted
 * archive wrote outside the directory it was unpacked into. So the unpacker
 * is not trusted with the system: it may read the system's programs and
 * libraries, read the one file it was given, and create ordinary files,
 * folders and links under the one directory it unpacks into. Nothing else.
 */
#ifndef DEV_SANDBOX_H
#define DEV_SANDBOX_H

enum dev_confinement {
	DEV_CONFINE_NONE = 0,     /* nothing could be applied */
	DEV_CONFINE_NOBODY = 1,   /* runs as the unprivileged user "nobody" */
	DEV_CONFINE_LANDLOCK = 2  /* the kernel limits which files it can touch */
};

/* Which confinement dev_confine_self would apply on this system. */
enum dev_confinement dev_confine_probe(void);

const char *dev_confine_name(enum dev_confinement how);

/*
 * Call in a child process, after fork and before exec. read_path is the file
 * or directory the child has to read. write_dir is the directory it may
 * write under, or NULL when it writes nowhere. Returns what was applied, or
 * -1 when the child must not continue: confinement was needed and could not
 * be set up.
 */
int dev_confine_self(const char *read_path, const char *write_dir);

/* For the parent in DEV_CONFINE_NOBODY mode: hand write_dir to that user. */
void dev_confine_prepare_dir(const char *write_dir);

/*
 * Runs one program confined and waits for it. It may read read_path and write
 * under write_dir, as dev_confine_self says. Returns its exit status, 125
 * when the confinement could not be set up, and -1 when it could not be
 * started at all.
 */
int dev_confined_run(char *const argv[], const char *read_path, const char *write_dir);

/*
 * Runs "producer | consumer", each confined on its own: the producer may read
 * read_path and write nowhere, and the consumer runs in write_dir and may
 * write only there. Returns 0 when both exited 0, 125 when either could not
 * be confined, and otherwise 1.
 */
int dev_confined_pipe(char *const producer[], const char *read_path,
	char *const consumer[], const char *write_dir);

/*
 * After a package has been unpacked into root: clears set-user-ID and
 * set-group-ID bits, removes devices, pipes and sockets, and gives everything
 * to the user who is installing. Links are left as they are. Nothing outside
 * root is touched, and no link is followed.
 */
void dev_tree_settle(const char *root);

#endif
