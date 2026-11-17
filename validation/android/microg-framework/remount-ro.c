/* Owned, rooted validation VM only. No setuid installation or arbitrary target. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *target = "/system/framework/services.jar";
    if (geteuid() != 0 || argc != 2 ||
            (strcmp(argv[1], "readonly") && strcmp(argv[1], "restore"))) {
        fputs("Use readonly or restore in the owned, rooted Android validation VM\n", stderr);
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
    if (!strcmp(argv[1], "restore")) {
        if (umount2(target, MNT_DETACH)) {
            perror("restore original framework mount");
            return 1;
        }
    } else if (mount(NULL, target, NULL,
            MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOATIME, NULL)) {
        perror("make validation framework bind read-only");
        return 1;
    }
    return 0;
}
