using System.Windows;
using WindowDisplay.Services;
using WindowDisplay.ViewModels;

namespace WindowDisplay.Views;

public partial class SettingsWindow : Window
{
    private readonly AppSettings _settings;

    public SettingsWindow()
    {
        InitializeComponent();
        _settings = AppSettings.Load();
        StartHostBox.IsChecked = _settings.StartHostOnOpen;
        KeepOnTopBox.IsChecked = _settings.KeepViewersOnTop;
        DropAppsBox.IsChecked = _settings.DropAppsOntoViewer;
        DoubleClickBox.IsChecked = _settings.DoubleClickToCaptureMouse;
        HoverFadeBox.IsChecked = _settings.FadePipOnHover;
    }

    private async void Diagnostics_Click(object sender, RoutedEventArgs e)
    {
        var displays = (Application.Current.MainWindow?.DataContext as MainViewModel)?.Displays.Select(x => x.Display) ?? [];
        var path = await new DiagnosticsService().CreateReportAsync(displays);
        MessageBox.Show($"Diagnostic report saved to:{Environment.NewLine}{path}", "MultiBox Viewer");
    }

    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        _settings.StartHostOnOpen = StartHostBox.IsChecked == true;
        _settings.KeepViewersOnTop = KeepOnTopBox.IsChecked == true;
        _settings.DropAppsOntoViewer = DropAppsBox.IsChecked == true;
        _settings.DoubleClickToCaptureMouse = DoubleClickBox.IsChecked == true;
        _settings.FadePipOnHover = HoverFadeBox.IsChecked == true;
        _settings.Save();
        try
        {
            var client = new HostClient();
            await client.SetAlwaysOnTopAsync(_settings.KeepViewersOnTop);
            await client.SetDropWindowsAsync(_settings.DropAppsOntoViewer);
            await client.SetViewerInteractionAsync(
                _settings.DoubleClickToCaptureMouse,
                _settings.FadePipOnHover);
        }
        catch
        {
            // Host may not be running yet; setting is persisted for next viewers.
        }
        DialogResult = true;
    }
}
