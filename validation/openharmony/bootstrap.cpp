/* Standard screen-lock client for an unprotected, owned SDK test image. */
#include "screenlock_manager.h"
#include "screenlock_callback_stub.h"
#include "ipc_skeleton.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <cstring>
#include <sys/utsname.h>
#include <dlfcn.h>
#include <unistd.h>
#include "nativetoken_kit.h"
#include "token_setproc.h"
#include "power_state_machine_info.h"
#include "power_errors.h"
class Callback final : public OHOS::ScreenLock::ScreenLockCallbackStub {
public:
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    int result = -1;
    void OnCallBack(int32_t code) override {
        std::lock_guard<std::mutex> lock(mutex);
        result = code;
        done = true;
        changed.notify_one();
    }
};
int main(int argc, char **argv) {
    bool app_profile = argc == 2 && std::strcmp(argv[1], "--app-runtime") == 0;
    bool status_only = argc == 2 && std::strcmp(argv[1], "--status") == 0;
    bool configure_profile = app_profile || (argc == 2 && std::strcmp(argv[1], "--full-runtime") == 0);
    if (argc != 1 && !configure_profile && !status_only) return 1;
    struct utsname machine;
    char command_line[16384] = {0};
    FILE *kernel = std::fopen("/proc/cmdline", "r");
    if (!kernel) return 1;
    bool read_kernel = std::fgets(command_line, sizeof(command_line), kernel) != nullptr;
    std::fclose(kernel);
    if (!read_kernel || uname(&machine) || std::strcmp(machine.machine, "x86_64") ||
            !std::strstr(command_line, "ohos.boot.hardware=x86_general")) return 1;
    auto manager = OHOS::ScreenLock::ScreenLockManager::GetInstance();
    if (!manager) return 2;
    bool locked = manager->IsScreenLocked();
    bool secure = manager->GetSecure();
    std::printf("credential_configured=%d screen_locked=%d\n", secure, locked);
    std::fflush(stdout);
    if (status_only) return 0;
    if (secure) { std::puts("Authentication required; no unlock attempted"); return 3; }
    if (!locked && !configure_profile) return 0;
    if (geteuid() != 0) return 7;
    const char *permissions[] = {"ohos.permission.ACCESS_SCREEN_LOCK", "ohos.permission.POWER_MANAGER"};
    NativeTokenInfoParams info = {0, 2, 0, nullptr, permissions, nullptr,
        "olympium-sdk-bootstrap", "system_core"};
    uint64_t original_token = GetSelfTokenID();
    uint64_t own_token = GetAccessTokenId(&info);
    int token_result = own_token ? SetSelfTokenID(own_token) : -1;
    std::printf("own_service_token_set=%d\n", token_result);
    std::fflush(stdout);
    if (token_result) return 8;
    struct ResetToken { uint64_t old; ~ResetToken() { SetSelfTokenID(old); } } reset{original_token};
    void *kit = dlopen("libaccesstoken_sdk.z.so", RTLD_NOW);
    auto reload = kit ? reinterpret_cast<int (*)()>(dlsym(kit,
        "_ZN4OHOS8Security11AccessToken14AccessTokenKit21ReloadNativeTokenInfoEv")) : nullptr;
    int reload_result = reload ? reload() : -1;
    std::printf("own_service_profile_reload=%d\n", reload_result);
    std::fflush(stdout);
    if (reload_result) return 9;
    if (configure_profile) {
        int configure = manager->SetScreenLockDisabled(app_profile, 100);
        bool disabled = false;
        int query = manager->IsScreenLockDisabled(100, disabled);
        std::printf("app_profile_configure=%d query=%d lock_ui_disabled=%d\n", configure, query, disabled);
        std::fflush(stdout);
        if (configure != OHOS::ScreenLock::E_SCREENLOCK_OK || query != OHOS::ScreenLock::E_SCREENLOCK_OK || disabled != app_profile) return 12;
    }
    void *power = dlopen("libpowermgr_client.z.so", RTLD_NOW);
    auto instance = power ? reinterpret_cast<void *(*)()>(dlsym(power,
        "_ZN4OHOS8PowerMgr14PowerMgrClient11GetInstanceEv")) : nullptr;
    auto wake = power ? reinterpret_cast<OHOS::PowerMgr::PowerErrors (*)(void *, OHOS::PowerMgr::WakeupDeviceType,
        const std::string &, const std::string &)>(dlsym(power,
        "_ZN4OHOS8PowerMgr14PowerMgrClient12WakeupDeviceENS0_16WakeupDeviceTypeERKNSt3__h12basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_")) : nullptr;
    auto screen_on = power ? reinterpret_cast<bool (*)(void *, bool)>(dlsym(power,
        "_ZN4OHOS8PowerMgr14PowerMgrClient10IsScreenOnEb")) : nullptr;
    if (!instance || !wake || !screen_on) return 10;
    void *power_manager = instance();
    auto keep_active = reinterpret_cast<OHOS::PowerMgr::PowerErrors (*)(void *, int64_t, const std::string &)>(dlsym(power,
        "_ZN4OHOS8PowerMgr14PowerMgrClient21OverrideScreenOffTimeElRKNSt3__h12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE"));
    if (!keep_active) return 13;
    int keep_result = static_cast<int>(keep_active(power_manager, 600000, std::string("22")));
    std::printf("qa_screen_off_override_ms=600000 result=%d\n", keep_result);
    std::fflush(stdout);
    if (keep_result) return 14;

    int wake_result = static_cast<int>(wake(power_manager, OHOS::PowerMgr::WakeupDeviceType::WAKEUP_DEVICE_APPLICATION,
        std::string("Olympium SDK bootstrap"), std::string("22")));
    std::printf("normal_wakeup_result=%d screen_on=%d\n", wake_result, screen_on(power_manager, true));
    std::fflush(stdout);
    if (wake_result) return 11;
    if (app_profile) {
        // The harness performs an ordinary gesture and verifies the resulting
        // app-owned UI. Do not race that gesture with an asynchronous Unlock.
        std::puts("app_profile_ready_for_normal_gesture");
        return 0;
    }
    if (!manager->IsScreenLocked()) return 0;
    OHOS::sptr<Callback> callback = new Callback();
    std::thread([] { OHOS::IPCSkeleton::JoinWorkThread(); }).detach();
    int rc = manager->Unlock(OHOS::ScreenLock::Action::UNLOCKSCREEN, callback);
    std::printf("normal_unlock_request=%d\n", rc);
    std::fflush(stdout);
    if (rc != OHOS::ScreenLock::E_SCREENLOCK_OK) return 4;
    std::unique_lock<std::mutex> lock(callback->mutex);
    if (!callback->changed.wait_for(lock, std::chrono::seconds(30), [&] { return callback->done; })) {
        std::puts("normal_unlock_callback_timeout"); return 5;
    }
    std::printf("normal_unlock_callback=%d screen_locked=%d\n", callback->result, manager->IsScreenLocked());
    return callback->result == 0 && !manager->IsScreenLocked() ? 0 : 6;
}
