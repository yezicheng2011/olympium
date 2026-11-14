// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

"use strict";

function mustRefuse(host, guest, level) {
  if (level === "refused") {
    return true;
  }
  var appleGuest = guest === "macos" || guest === "ios";
  if (appleGuest && host !== "macos") {
    return true;
  }
  if (guest === "openharmony" && host === "android") {
    return true;
  }
  return host === "openharmony" && guest !== "openharmony";
}

function refusalMessage(host, guest) {
  if (guest === "openharmony" && host === "android") {
    return "Olympium will not run OpenHarmony software on Android. Android is not an OpenHarmony runtime, and Olympium does not translate a HAP into an Android app.";
  }
  if (host === "openharmony" && guest !== "openharmony") {
    return "On an OpenHarmony device, Olympium runs OpenHarmony software with the system itself. It does not run other guest systems there.";
  }
  return "Olympium will not run macOS or iOS software on " + hostLabel(host) + ". Apple licenses those systems for Apple hardware. Olympium does not include an emulator, disk image, translation layer, or installer for them.";
}

function hostLabel(host) {
  switch (host) {
    case "android":
      return "Android";
    case "windows":
      return "Windows";
    case "linux":
      return "Linux";
    case "macos":
      return "macOS";
    case "openharmony":
      return "OpenHarmony";
    default:
      return host;
  }
}

module.exports = {
  mustRefuse: mustRefuse,
  refusalMessage: refusalMessage,
  hostLabel: hostLabel,
};
