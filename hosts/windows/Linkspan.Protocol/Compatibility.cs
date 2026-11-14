// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

using System.Text.Json;

namespace Linkspan.Protocol;

public sealed record MatrixCell(
    string Host,
    string Guest,
    string Level,
    string Title,
    string Summary,
    string LegalNote);

public sealed record LimitNote(string Id, string Title, string Body);

public sealed class CompatibilityDocument
{
    public int Schema { get; init; }
    public string Product { get; init; } = "";
    public string License { get; init; } = "";
    public IReadOnlyList<MatrixCell> Cells { get; init; } = Array.Empty<MatrixCell>();
    public IReadOnlyList<LimitNote> Limits { get; init; } = Array.Empty<LimitNote>();

    public static CompatibilityDocument Load(string path)
    {
        using var stream = File.OpenRead(path);
        using var document = JsonDocument.Parse(stream);
        var root = document.RootElement;
        var cells = new List<MatrixCell>();
        foreach (var cell in root.GetProperty("cells").EnumerateArray())
        {
            cells.Add(new MatrixCell(
                cell.GetProperty("host").GetString() ?? "",
                cell.GetProperty("guest").GetString() ?? "",
                cell.GetProperty("level").GetString() ?? "",
                cell.GetProperty("title").GetString() ?? "",
                cell.GetProperty("summary").GetString() ?? "",
                cell.GetProperty("legal_note").GetString() ?? ""));
        }

        var limits = new List<LimitNote>();
        foreach (var limit in root.GetProperty("limits").EnumerateArray())
        {
            limits.Add(new LimitNote(
                limit.GetProperty("id").GetString() ?? "",
                limit.GetProperty("title").GetString() ?? "",
                limit.GetProperty("body").GetString() ?? ""));
        }

        return new CompatibilityDocument
        {
            Schema = root.GetProperty("schema").GetInt32(),
            Product = root.GetProperty("product").GetString() ?? "",
            License = root.GetProperty("license").GetString() ?? "",
            Cells = cells,
            Limits = limits,
        };
    }

    public MatrixCell? Find(string host, string guest)
    {
        foreach (var cell in Cells)
        {
            if (cell.Host == host && cell.Guest == guest)
            {
                return cell;
            }
        }

        return null;
    }

    public static string? FindShareFile(string startDirectory)
    {
        var dir = new DirectoryInfo(startDirectory);
        while (dir is not null)
        {
            var candidate = Path.Combine(dir.FullName, "share", "compatibility.json");
            if (File.Exists(candidate))
            {
                return candidate;
            }

            dir = dir.Parent;
        }

        return null;
    }
}
