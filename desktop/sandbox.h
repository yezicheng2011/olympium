/*
 * Copyright 2026 Development Desktop contributors
 * License: daniel.ye Source License, Version 1.0. See LICENSE.
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

#endif
