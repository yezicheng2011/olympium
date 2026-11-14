// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using Linkspan.Protocol;
using Xunit;

namespace Linkspan.Protocol.Tests;

public class LaunchGateTests
{
    [Theory]
    [InlineData("linux", "macos", "ready")]
    [InlineData("linux", "ios", "limited")]
    [InlineData("windows", "macos", "engine")]
    [InlineData("android", "ios", "scaffold")]
        [InlineData("linux", "linux", "refused")]
        [InlineData("android", "openharmony", "ready")]
        [InlineData("openharmony", "windows", "engine")]
        public void RefusesAppleGuestsOffAppleHardwareEvenIfTheFileIsWrong(string host, string guest, string level)
    {
        Assert.True(LaunchGate.MustRefuse(host, guest, level));
    }

    [Theory]
    [InlineData("macos", "macos", "ready")]
    [InlineData("macos", "ios", "limited")]
    [InlineData("linux", "windows", "engine")]
        [InlineData("android", "android", "ready")]
        [InlineData("linux", "openharmony", "limited")]
        [InlineData("openharmony", "openharmony", "ready")]
        public void AllowsCombinationsTheEngineMarksAsRunnable(string host, string guest, string level)
    {
        Assert.False(LaunchGate.MustRefuse(host, guest, level));
    }

    [Fact]
    public void CheckedInDocumentMatchesTheContract()
    {
        var path = CompatibilityDocument.FindShareFile(AppContext.BaseDirectory);
        Assert.NotNull(path);
        var document = CompatibilityDocument.Load(path!);
        Assert.Equal(1, document.Schema);
        Assert.Equal("Linkspan", document.Product);
        Assert.Equal("Apache-2.0", document.License);
        Assert.Equal(30, document.Cells.Count);
        foreach (var cell in document.Cells)
        {
            if ((cell.Guest is "macos" or "ios") && cell.Host != "macos")
            {
                Assert.Equal("refused", cell.Level);
                Assert.Contains("Linkspan will not run macOS or iOS software", cell.LegalNote);
            }
        }

        Assert.Equal("ready", document.Find("linux", "linux")!.Level);
        Assert.Equal("limited", document.Find("macos", "ios")!.Level);
        Assert.Contains(document.Limits, limit => limit.Id == "no-ios-host");
    }
}
