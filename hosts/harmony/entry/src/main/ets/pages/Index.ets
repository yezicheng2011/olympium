// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

import { LaunchGate } from '../common/LaunchGate';

@Entry
@Component
struct Index {
  @State note: string = 'Linkspan on OpenHarmony installs OpenHarmony packages with the system installer. Guest images are not bundled in this app.';
  @State guest: string = 'openharmony';
  @State bundleName: string = '';
  @State developerMode: string = 'Developer mode has not been read yet.';

  aboutToAppear(): void {
    this.readDeveloperMode();
  }

  build() {
    Column() {
      Text('Linkspan')
        .fontSize(28)
        .fontWeight(FontWeight.Bold)
        .width('100%')
      Text(this.note)
        .fontSize(16)
        .width('100%')
        .margin({ top: 12, bottom: 12 })
      Text(this.developerMode)
        .fontSize(14)
        .width('100%')
        .margin({ bottom: 12 })
      TextInput({ placeholder: 'Guest system', text: this.guest })
        .onChange((value: string) => {
          this.guest = value;
        })
        .margin({ bottom: 8 })
      TextInput({ placeholder: 'Bundle name, such as com.example.app', text: this.bundleName })
        .onChange((value: string) => {
          this.bundleName = value;
        })
        .margin({ bottom: 8 })
      Button('Check whether this guest can run')
        .onClick(() => {
          this.explain();
        })
        .margin({ bottom: 8 })
      Button('Install a HAP you already have')
        .onClick(() => {
          this.installNoted();
        })
    }
    .width('100%')
    .height('100%')
    .padding(24)
    .alignItems(HorizontalAlign.Start)
    .backgroundColor($r('app.color.page_background'))
  }

  explain(): void {
    const guest = this.guest.length === 0 ? 'openharmony' : this.guest.trim();
    if (LaunchGate.mustRefuse('openharmony', guest, 'ready')) {
      this.note = LaunchGate.refusalMessage('openharmony', guest);
      return;
    }
    this.note = 'OpenHarmony software can be installed with the system bundle installer when developer mode allows it. Linkspan does not translate the package and does not download a HarmonyOS image.';
  }

  installNoted(): void {
    if (LaunchGate.mustRefuse('openharmony', 'openharmony', 'ready')) {
      this.note = LaunchGate.refusalMessage('openharmony', 'openharmony');
      return;
    }
    const name = this.bundleName.trim();
    if (name.length === 0) {
      this.note = 'Choose a HAP from hdc on a computer in developer mode: hdc install entry.hap. This app does not embed a guest image. See the OpenHarmony host manual.';
      return;
    }
    this.note = 'Ask OpenHarmony to open ' + name + ' after hdc install. Unsigned packages are refused unless developer mode is on. Linkspan does not add a certificate.';
  }

  readDeveloperMode(): void {
    this.developerMode = 'Unsigned HAPs install only in developer mode, with hdc install. This page does not turn developer mode on and it does not add a certificate.';
  }
}
