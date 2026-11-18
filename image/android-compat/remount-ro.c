/* Owned, rooted validation VM only. No setuid installation or arbitrary target. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *target = "/system/framework/services.jar";
    if (geteuid() != 0 || argc != 2 ||
            (strcmp(argv[1], "readonly") && strcmp(argv[1], "restore") && strcmp(argv[1], "install"))) {
        fputs("Use install, readonly or restore in the owned Android runtime\n", stderr);
        return 2;
    }
    int ns = open("/proc/1/ns/mnt", O_RDONLY | O_CLOEXEC);
    int root = open("/proc/1/root", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (ns < 0 || root < 0 || setns(ns, CLONE_NEWNS) || fchdir(root) || chroot(".") || chdir("/")) {
        perror("enter validation init filesystem");
        return 1;
    }
    close(ns);
    close(root);
    if (!strcmp(argv[1], "install")) {
        const char *source = "/data/misc/olympium-framework/services.jar";
        struct stat st;
        if (lstat(source, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
                (st.st_mode & 0222) || st.st_size < 1 || st.st_size > 64 * 1024 * 1024) {
            fputs("Invalid managed Android framework file\n", stderr);
            return 1;
        }
        if (mount(source, target, NULL, MS_BIND, NULL)) {
            perror("bind managed Android framework");
            return 1;
        }
    }
    if (!strcmp(argv[1], "restore")) {
        if (umount2(target, MNT_DETACH)) {
            perror("restore original framework mount");
            return 1;
        }
    } else if (mount(NULL, target, NULL,
            MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOATIME, NULL)) {
        perror("make validation framework bind read-only");
        if (!strcmp(argv[1], "install")) umount2(target, MNT_DETACH);
        return 1;
    }
    return 0;
}
