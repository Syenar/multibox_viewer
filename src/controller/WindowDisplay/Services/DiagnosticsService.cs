using System.IO.Compression;
using System.Text;
using WindowDisplay.Models;

namespace WindowDisplay.Services;

public sealed class DiagnosticsService
{
    public async Task<string> CreateReportAsync(IEnumerable<DisplayInfo> displays, Exception? error = null)
    {
        var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WindowDisplay", "diagnostics");
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, $"WindowDisplay-{DateTime.Now:yyyyMMdd-HHmmss}.zip");
        await using var stream = File.Create(path);
        using var archive = new ZipArchive(stream, ZipArchiveMode.Create);
        var entry = archive.CreateEntry("report.txt");
        await using var writer = new StreamWriter(entry.Open(), Encoding.UTF8);
        await writer.WriteLineAsync($"Created: {DateTimeOffset.Now:O}");
        await writer.WriteLineAsync($"OS: {Environment.OSVersion}");
        foreach (var display in displays) await writer.WriteLineAsync($"{display.Name}: {display.ResolutionLabel}, {display.State}");
        if (error is not null) await writer.WriteLineAsync(error.ToString());
        return path;
    }
}
