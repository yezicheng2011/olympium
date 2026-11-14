// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using System.Diagnostics;
using System.Text.Json;

namespace Olympium.Protocol;

public sealed class OlympiumCli
{
    public OlympiumCli(string executable)
    {
        Executable = executable;
    }

    public string Executable { get; }

    public static OlympiumCli? Locate()
    {
        foreach (var key in new[] { "OLYMPIUM_CLI", "LINKSPAN_CLI" })
        {
            var fromEnv = Environment.GetEnvironmentVariable(key);
            if (!string.IsNullOrWhiteSpace(fromEnv) && File.Exists(fromEnv))
            {
                return new OlympiumCli(fromEnv);
            }
        }

        var path = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrEmpty(path))
        {
            return null;
        }

        foreach (var dir in path.Split(Path.PathSeparator))
        {
            foreach (var name in new[] { "olympium.exe", "olympium", "linkspan.exe", "linkspan" })
            {
                var candidate = Path.Combine(dir, name);
                if (File.Exists(candidate))
                {
                    return new OlympiumCli(candidate);
                }
            }
        }

        return null;
    }

    public async Task<JsonDocument> RunAsync(params string[] args)
    {
        var start = new ProcessStartInfo
        {
            FileName = Executable,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        start.ArgumentList.Add("--json");
        foreach (var arg in args)
        {
            start.ArgumentList.Add(arg);
        }

        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start Olympium.");
        var stdout = await process.StandardOutput.ReadToEndAsync().ConfigureAwait(false);
        var stderr = await process.StandardError.ReadToEndAsync().ConfigureAwait(false);
        await process.WaitForExitAsync().ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(stdout))
        {
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(stderr)
                ? "Olympium returned no JSON."
                : stderr.Trim());
        }

        return JsonDocument.Parse(stdout);
    }
}
