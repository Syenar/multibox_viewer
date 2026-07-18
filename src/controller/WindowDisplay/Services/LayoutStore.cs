using System.Text.Json;
using WindowDisplay.Models;

namespace WindowDisplay.Services;

public sealed class LayoutStore
{
    private static readonly string Root = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WindowDisplay", "layouts");
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public async Task SaveAsync(LayoutInfo layout)
    {
        Directory.CreateDirectory(Root);
        var file = SafeName(layout.Name) + ".json";
        await File.WriteAllTextAsync(Path.Combine(Root, file), JsonSerializer.Serialize(layout, JsonOptions));
    }

    public async Task<List<LayoutInfo>> LoadAllAsync()
    {
        if (!Directory.Exists(Root)) return [];
        var layouts = new List<LayoutInfo>();
        foreach (var file in Directory.EnumerateFiles(Root, "*.json"))
        {
            var layout = JsonSerializer.Deserialize<LayoutInfo>(await File.ReadAllTextAsync(file));
            if (layout is not null) layouts.Add(layout);
        }
        return layouts.OrderByDescending(x => x.SavedAt).ToList();
    }

    private static string SafeName(string value) => string.Concat(value.Select(c => Path.GetInvalidFileNameChars().Contains(c) ? '_' : c));
}
