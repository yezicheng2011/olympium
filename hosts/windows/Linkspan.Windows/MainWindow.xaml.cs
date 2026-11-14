// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Linkspan.Protocol;

namespace Linkspan.Windows;

public partial class MainWindow : Window
{
    private readonly CompatibilityDocument? _document;
    private readonly string? _documentError;
    private bool _ready;

    public MainWindow()
    {
        InitializeComponent();
        try
        {
            var bundled = Path.Combine(AppContext.BaseDirectory, "compatibility.json");
            var path = File.Exists(bundled)
                ? bundled
                : CompatibilityDocument.FindShareFile(AppContext.BaseDirectory);
            _document = path is null ? null : CompatibilityDocument.Load(path);
            _documentError = _document is null ? "The compatibility file was not found next to Linkspan." : null;
        }
        catch (Exception ex) when (ex is IOException or System.Text.Json.JsonException or InvalidOperationException)
        {
            _documentError = ex.Message;
        }

        AppearanceBox.SelectedIndex = Theme.LoadChoice() switch
        {
            ThemeChoice.Light => 1,
            ThemeChoice.Dark => 2,
            _ => 0,
        };
        _ready = true;
        ShowOverview(this, new RoutedEventArgs());
    }

    private void AppearanceChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_ready || AppearanceBox.SelectedItem is not ComboBoxItem item || item.Tag is not string tag)
        {
            return;
        }

        var choice = tag switch
        {
            "light" => ThemeChoice.Light,
            "dark" => ThemeChoice.Dark,
            _ => ThemeChoice.System,
        };
        Theme.SaveChoice(choice);
        Theme.Apply(choice);
    }

    private void ShowOverview(object sender, RoutedEventArgs e)
    {
        Content.Children.Clear();
        AddHeading("Programs from other systems, on this Windows computer.");
        AddBody("Windows programs run directly. Linux programs go through WSL when it is installed. Android APKs use Waydroid or adb if you already have one. OpenHarmony packages use a real OpenHarmony runtime after the slim image is downloaded. Mac and iOS software can be listed here and will not be started. Wine is not required on Windows.");
        var cli = LinkspanCli.Locate();
        AddBody(cli is null
            ? "The Linkspan engine is not on PATH. Build it with cargo on this computer, then set LINKSPAN_CLI or add linkspan.exe to PATH. The compatibility list below still works."
            : "Engine: " + cli.Executable);
        if (_documentError is not null)
        {
            AddBody(_documentError);
        }
    }

    private void ShowPrograms(object sender, RoutedEventArgs e)
    {
        Content.Children.Clear();
        AddHeading("Programs");
        AddBody("Add a program from the command line: linkspan add --file <path>. Then run it with linkspan run <id>. This window shows the result when the engine is on PATH.");
        var cli = LinkspanCli.Locate();
        if (cli is null)
        {
            AddBody("No engine was found, so there is no library to show yet.");
            return;
        }

        try
        {
            var json = cli.RunAsync("list").GetAwaiter().GetResult();
            var programs = json.RootElement.GetProperty("data").GetProperty("programs");
            if (programs.GetArrayLength() == 0)
            {
                AddBody("No programs yet.");
                return;
            }

            foreach (var program in programs.EnumerateArray())
            {
                var name = program.GetProperty("name").GetString();
                var guest = program.GetProperty("guest").GetString() ?? "";
                var level = _document?.Find("windows", guest)?.Level ?? "unknown";
                var refused = LaunchGate.MustRefuse("windows", guest, level);
                AddBody((refused ? "Will not run — " : "") + name + " (" + guest + ")");
            }
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or System.Text.Json.JsonException or KeyNotFoundException)
        {
            AddBody(ex.Message);
        }
    }

    private void ShowCompatibility(object sender, RoutedEventArgs e)
    {
        Content.Children.Clear();
        AddHeading("Compatibility");
        if (_document is null)
        {
            AddBody(_documentError ?? "No compatibility data.");
            return;
        }

        foreach (var cell in _document.Cells)
        {
            var line = cell.Host + " / " + cell.Guest + " — " + cell.Title;
            AddBody(line);
            AddMuted(cell.Summary);
            if (LaunchGate.MustRefuse(cell.Host, cell.Guest, cell.Level) && cell.LegalNote.Length > 0)
            {
                AddMuted(cell.LegalNote);
            }
        }
    }

    private void ShowLimits(object sender, RoutedEventArgs e)
    {
        Content.Children.Clear();
        AddHeading("Limits");
        if (_document is null)
        {
            AddBody(_documentError ?? "No limits were loaded.");
            return;
        }

        foreach (var limit in _document.Limits)
        {
            AddSubheading(limit.Title);
            AddBody(limit.Body);
        }
    }

    private void AddHeading(string text) => Content.Children.Add(MakeText(text, 28, FontWeights.SemiBold, 0, 12));

    private void AddSubheading(string text) => Content.Children.Add(MakeText(text, 18, FontWeights.SemiBold, 8, 4));

    private void AddBody(string text) => Content.Children.Add(MakeText(text, 14, FontWeights.Normal, 0, 8));

    private void AddMuted(string text)
    {
        var block = MakeText(text, 13, FontWeights.Normal, 0, 10);
        block.Foreground = (Brush)FindResource("AppMuted");
        Content.Children.Add(block);
    }

    private static TextBlock MakeText(string text, double size, FontWeight weight, double top, double bottom)
    {
        return new TextBlock
        {
            Text = text,
            FontSize = size,
            FontWeight = weight,
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, top, 0, bottom),
        };
    }
}
