// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

"use strict";

var assert = require("assert");
var gate = require("./launch-gate");

assert.strictEqual(gate.mustRefuse("linux", "macos", "ready"), true);
assert.strictEqual(gate.mustRefuse("android", "ios", "engine"), true);
assert.strictEqual(gate.mustRefuse("windows", "linux", "refused"), true);
assert.strictEqual(gate.mustRefuse("macos", "macos", "ready"), false);
assert.strictEqual(gate.mustRefuse("macos", "ios", "limited"), false);
assert.strictEqual(gate.mustRefuse("linux", "windows", "engine"), false);
assert.strictEqual(gate.mustRefuse("android", "openharmony", "ready"), true);
assert.strictEqual(gate.mustRefuse("openharmony", "android", "ready"), true);
assert.strictEqual(gate.mustRefuse("linux", "openharmony", "limited"), false);
assert.strictEqual(gate.mustRefuse("openharmony", "openharmony", "ready"), false);
assert.ok(gate.refusalMessage("android", "macos").indexOf("Linkspan will not run macOS or iOS software") >= 0);
assert.ok(gate.refusalMessage("android", "openharmony").indexOf("does not translate a HAP") >= 0);
assert.strictEqual(gate.hostLabel("openharmony"), "OpenHarmony");
console.log("launch-gate.test.js ok");
