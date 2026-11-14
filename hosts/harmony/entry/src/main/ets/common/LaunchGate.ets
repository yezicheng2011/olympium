// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

export class LaunchGate {
  static mustRefuse(host: string, guest: string, level: string): boolean {
    if (level === 'refused') {
      return true;
    }
    const appleGuest = guest === 'macos' || guest === 'ios';
    if (appleGuest && host !== 'macos') {
      return true;
    }
    if (guest === 'openharmony' && host === 'android') {
      return true;
    }
    return host === 'openharmony' && guest !== 'openharmony';
  }

  static refusalMessage(host: string, guest: string): string {
    if (guest === 'openharmony' && host === 'android') {
      return 'Linkspan will not run OpenHarmony software on Android. Android is not an OpenHarmony runtime, and Linkspan does not translate a HAP into an Android app.';
    }
    if (host === 'openharmony' && guest !== 'openharmony') {
      return 'On an OpenHarmony device, Linkspan runs OpenHarmony software with the system itself. It does not run other guest systems there.';
    }
    return 'Linkspan will not run macOS or iOS software on this computer. Apple licenses those systems for Apple hardware.';
  }
}
