using System.Text.Json;

namespace WindowDisplay.Services;

public sealed class AppSettings
{
    private static readonly string PathName = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "WindowDisplay",
        "settings.json");

    public bool StartHostOnOpen { get; set; } = true;
    public bool KeepViewersOnTop { get; set; } = true;
    /// <summary>When true, dragging a window title bar onto a PiP viewer moves that app to the virtual display.</summary>
    public bool DropAppsOntoViewer { get; set; } = true;
    /// <summary>When true, mouse is not injected until the user double-clicks the PiP image.</summary>
    public bool DoubleClickToCaptureMouse { get; set; } = true;
    /// <summary>When true, the PiP fades while the cursor hovers so content behind remains visible.</summary>
    public bool FadePipOnHover { get; set; } = true;

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(PathName))
            {
                var loaded = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(PathName));
                if (loaded is not null) return loaded;
            }
        }
        catch { /* keep defaults */ }
        return new AppSettings();
    }

    public void Save()
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(PathName)!);
        File.WriteAllText(PathName, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}
