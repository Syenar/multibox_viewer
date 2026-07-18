using System.Diagnostics;
using System.Windows;
using WindowDisplay.Models;
using WindowDisplay.Services;

namespace WindowDisplay.Views;

public partial class AppPickerDialog : Window
{
    private readonly DisplayInfo _display;
    private readonly List<AppWindowInfo> _apps;
    public AppPickerDialog(DisplayInfo display)
    {
        _display = display; InitializeComponent();
        _apps = new AppEnumerator().GetUserApps().ToList(); Apps.ItemsSource = _apps;
    }
    private void Search_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        var term = SearchBox.Text.Trim();
        Apps.ItemsSource = _apps.Where(x => x.DisplayName.Contains(term, StringComparison.OrdinalIgnoreCase));
    }
    private async void Move_Click(object sender, RoutedEventArgs e)
    {
        if (Apps.SelectedItem is not AppWindowInfo app) return;
        try
        {
            await new WindowMover().MoveToDisplayAsync(app, _display);
            DialogResult = true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Couldn't move app", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }
    private void Browse_Click(object sender, RoutedEventArgs e) => Process.Start(new ProcessStartInfo("explorer.exe") { UseShellExecute = true });
}
