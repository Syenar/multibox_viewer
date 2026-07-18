using System.Windows;
using System.Windows.Media;

namespace WindowDisplay.Helpers;

public static class ThemeManager
{
    private const string ThemeFile = "theme.txt";
    private static string ThemePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WindowDisplay", ThemeFile);
    public static bool IsDark => File.Exists(ThemePath) &&
                                 string.Equals(File.ReadAllText(ThemePath), "dark", StringComparison.OrdinalIgnoreCase);

    public static void ApplySavedTheme() => Apply(IsDark);

    public static void Toggle() => Apply(!IsDark);

    public static void Apply(bool dark)
    {
        var resources = Application.Current.Resources;
        resources["CanvasBrush"] = Brush(dark ? "#FF17212A" : "#FFF4F7F9");
        resources["SurfaceBrush"] = Brush(dark ? "#FF22303B" : "#FFFFFFFF");
        resources["SurfaceMutedBrush"] = Brush(dark ? "#FF2C3B46" : "#FFE8EEF2");
        resources["TextBrush"] = Brush(dark ? "#FFF0F5F8" : "#FF17222C");
        resources["SubtleTextBrush"] = Brush(dark ? "#FFB2C0CA" : "#FF5B6874");
        resources["BorderBrush"] = Brush(dark ? "#FF40515E" : "#FFD8E0E6");
        resources["AccentBrush"] = Brush(dark ? "#FF3B9AC3" : "#FF176B93");
        Directory.CreateDirectory(Path.GetDirectoryName(ThemePath)!);
        File.WriteAllText(ThemePath, dark ? "dark" : "light");
    }

    private static SolidColorBrush Brush(string hex) =>
        new((Color)ColorConverter.ConvertFromString(hex)!);
}
