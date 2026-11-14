// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using System.Windows;

namespace Olympium.Windows;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        Theme.Apply(Theme.LoadChoice());
        base.OnStartup(e);
    }
}
