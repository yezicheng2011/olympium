/* Diagnostic client for the normal account API in the owned public SDK VM. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "status") && strcmp(argv[1], "activate-sdk-user"))) return 2;
    struct utsname machine;
    char command_line[16384] = {0};
    FILE *kernel = fopen("/proc/cmdline", "r");
    if (!kernel) return 2;
    int read_kernel = fgets(command_line, sizeof(command_line), kernel) != NULL;
    fclose(kernel);
    if (!read_kernel || uname(&machine) || strcmp(machine.machine, "x86_64") ||
            !strstr(command_line, "ohos.boot.hardware=x86_general") || geteuid() != 0) return 2;
    void *lib = dlopen("libos_account_innerkits.z.so", RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 3; }
    int (*foreground)(int *) = (int (*)(int *))dlsym(lib, "_ZN4OHOS9AccountSA16OsAccountManager29GetForegroundOsAccountLocalIdERi");
    int (*activate)(int) = (int (*)(int))dlsym(lib, "_ZN4OHOS9AccountSA16OsAccountManager17ActivateOsAccountEi");
    if (!foreground || !activate) { fprintf(stderr, "SDK account client API missing\n"); return 4; }
    int user = -1;
    int result = foreground(&user);
    printf("foreground_result=%d foreground_user=%d\n", result, user);
    fflush(stdout);
    if (!strcmp(argv[1], "activate-sdk-user")) {
        result = activate(100);
        printf("normal_activation_result=%d\n", result);
        fflush(stdout);
        return result ? 1 : 0;
    }
    return 0;
}
