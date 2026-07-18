using System.Windows;

namespace WindowDisplay;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        Helpers.ThemeManager.ApplySavedTheme();
    }
}
