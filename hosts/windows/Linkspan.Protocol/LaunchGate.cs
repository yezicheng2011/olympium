// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

namespace Linkspan.Protocol;

/// <summary>
/// Client-side copy of the refusal rule. The Rust engine is the authority.
/// This check stays here so a damaged compatibility file cannot launch
/// macOS or iOS software on a non-Apple computer.
/// </summary>
public static class LaunchGate
{
    public static bool MustRefuse(string host, string guest, string level)
    {
        if (string.Equals(level, "refused", StringComparison.Ordinal))
        {
            return true;
        }

        var appleGuest = guest is "macos" or "ios";
        if (appleGuest && !string.Equals(host, "macos", StringComparison.Ordinal))
        {
            return true;
        }

        if (guest == "openharmony" && host == "android")
        {
            return true;
        }

        return host == "openharmony" && guest != "openharmony";
    }

    public static string RefusalMessage(string host)
    {
        return RefusalMessage(host, "macos");
    }

    public static string RefusalMessage(string host, string guest)
    {
        if (guest == "openharmony" && host == "android")
        {
            return "Linkspan will not run OpenHarmony software on Android. Android is not an OpenHarmony runtime, and Linkspan does not translate a HAP into an Android app.";
        }

        if (host == "openharmony" && guest != "openharmony")
        {
            return "On an OpenHarmony device, Linkspan runs OpenHarmony software with the system itself. It does not run other guest systems there.";
        }

        return "Linkspan will not run macOS or iOS software on " + HostLabel(host)
            + ". Apple licenses those systems for Apple hardware. Linkspan does not include an emulator, disk image, translation layer, or installer for them.";
    }

    public static string HostLabel(string host) => host switch
    {
        "android" => "Android",
        "windows" => "Windows",
        "linux" => "Linux",
        "macos" => "macOS",
        "openharmony" => "OpenHarmony",
        _ => host,
    };
}
