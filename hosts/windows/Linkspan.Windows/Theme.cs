// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using System.IO;
using System.Windows;
using System.Windows.Media;
using Microsoft.Win32;

namespace Linkspan.Windows;

public enum ThemeChoice
{
    System,
    Light,
    Dark,
}

public static class Theme
{
    public static ThemeChoice LoadChoice()
    {
        try
        {
            var text = File.ReadAllText(SettingsPath()).Trim().ToLowerInvariant();
            return text switch
            {
                "light" => ThemeChoice.Light,
                "dark" => ThemeChoice.Dark,
                _ => ThemeChoice.System,
            };
        }
        catch (IOException)
        {
            return ThemeChoice.System;
        }
    }

    public static void SaveChoice(ThemeChoice choice)
    {
        var dir = Path.GetDirectoryName(SettingsPath());
        if (!string.IsNullOrEmpty(dir))
        {
            Directory.CreateDirectory(dir);
        }

        var text = choice switch
        {
            ThemeChoice.Light => "light",
            ThemeChoice.Dark => "dark",
            _ => "system",
        };
        File.WriteAllText(SettingsPath(), text);
    }

    public static void Apply(ThemeChoice choice)
    {
        var dark = choice switch
        {
            ThemeChoice.Light => false,
            ThemeChoice.Dark => true,
            _ => !SystemUsesLightTheme(),
        };
        Set("AppBackground", dark ? "#1F1F1F" : "#F3F3F3");
        Set("AppSurface", dark ? "#2B2B2B" : "#FFFFFF");
        Set("AppText", dark ? "#F3F3F3" : "#1A1A1A");
        Set("AppMuted", dark ? "#C6C6C6" : "#5C5C5C");
        Set("AppAccent", dark ? "#4CC2FF" : "#0F6CBD");
        Set("AppLine", dark ? "#3F3F3F" : "#E0E0E0");
    }

    public static bool SystemUsesLightTheme()
    {
        try
        {
            var value = Registry.GetValue(
                @"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
                "AppsUseLightTheme",
                1);
            return value is int number && number != 0;
        }
        catch (System.Security.SecurityException)
        {
            return true;
        }
        catch (IOException)
        {
            return true;
        }
    }

    private static void Set(string key, string hex)
    {
        Application.Current.Resources[key] = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
    }

    private static string SettingsPath()
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        return Path.Combine(root, "Linkspan", "appearance.txt");
    }
}
