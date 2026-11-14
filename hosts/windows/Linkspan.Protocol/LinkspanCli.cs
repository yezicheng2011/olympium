// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using System.Diagnostics;
using System.Text.Json;

namespace Linkspan.Protocol;

public sealed class LinkspanCli
{
    public LinkspanCli(string executable)
    {
        Executable = executable;
    }

    public string Executable { get; }

    public static LinkspanCli? Locate()
    {
        var fromEnv = Environment.GetEnvironmentVariable("LINKSPAN_CLI");
        if (!string.IsNullOrWhiteSpace(fromEnv) && File.Exists(fromEnv))
        {
            return new LinkspanCli(fromEnv);
        }

        var path = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrEmpty(path))
        {
            return null;
        }

        foreach (var dir in path.Split(Path.PathSeparator))
        {
            foreach (var name in new[] { "linkspan.exe", "linkspan" })
            {
                var candidate = Path.Combine(dir, name);
                if (File.Exists(candidate))
                {
                    return new LinkspanCli(candidate);
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

        using var process = Process.Start(start) ?? throw new InvalidOperationException("Could not start Linkspan.");
        var stdout = await process.StandardOutput.ReadToEndAsync().ConfigureAwait(false);
        var stderr = await process.StandardError.ReadToEndAsync().ConfigureAwait(false);
        await process.WaitForExitAsync().ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(stdout))
        {
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(stderr)
                ? "Linkspan returned no JSON."
                : stderr.Trim());
        }

        return JsonDocument.Parse(stdout);
    }
}
