using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows;
using WindowDisplay.Helpers;
using WindowDisplay.Models;
using WindowDisplay.Services;
using WindowDisplay.Views;

namespace WindowDisplay.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private string _statusText = "Connecting…";
    private readonly Dictionary<int, string> _customNames = [];

    public ObservableCollection<DisplayCardViewModel> Displays { get; } = [];
    public bool HasDisplays => Displays.Count > 0;
    public bool IsEmpty => !HasDisplays;
    public string StatusText { get => _statusText; private set { _statusText = value; PropertyChanged?.Invoke(this, new(nameof(StatusText))); } }
    public RelayCommand NewDisplayCommand => new(async _ => await CreateDisplayAsync());
    public RelayCommand RestoreLayoutCommand => new(async _ => await RestoreLayoutAsync());
    public RelayCommand SaveLayoutCommand => new(async _ => await SaveLayoutAsync());
    public RelayCommand RefreshCommand => new(async _ => await RefreshAsync());
    public RelayCommand SettingsCommand => new(async _ =>
    {
        var window = new SettingsWindow { Owner = Application.Current.MainWindow };
        if (window.ShowDialog() == true)
        {
            try
            {
                var settings = AppSettings.Load();
                var client = new HostClient();
                await client.SetAlwaysOnTopAsync(settings.KeepViewersOnTop);
                await client.SetDropWindowsAsync(settings.DropAppsOntoViewer);
                await client.SetViewerInteractionAsync(
                    settings.DoubleClickToCaptureMouse,
                    settings.FadePipOnHover);
            }
            catch { /* host may be offline */ }
        }
    });
    public RelayCommand ToggleThemeCommand => new(_ => ThemeManager.Toggle());
    public event PropertyChangedEventHandler? PropertyChanged;

    public async Task InitializeAsync()
    {
        var settings = AppSettings.Load();
        if (settings.StartHostOnOpen)
        {
            try
            {
                var client = new HostClient();
                await client.EnsureHostAsync();
                await client.SetAlwaysOnTopAsync(settings.KeepViewersOnTop);
                await client.SetDropWindowsAsync(settings.DropAppsOntoViewer);
                await client.SetViewerInteractionAsync(
                    settings.DoubleClickToCaptureMouse,
                    settings.FadePipOnHover);
                StatusText = "Host connected";
            }
            catch (Exception ex)
            {
                StatusText = "Host not connected — " + ex.Message;
            }
        }
        await RefreshAsync();
    }

    public async Task RefreshAsync()
    {
        try
        {
            foreach (var card in Displays)
            {
                if (!string.IsNullOrWhiteSpace(card.Name))
                    _customNames[card.Display.ConnectorIndex] = card.Name;
            }

            var displays = await new HostClient().ListDisplaysAsync();
            Displays.Clear();
            foreach (var display in displays) Add(display);
            StatusText = displays.Count == 0
                ? "Ready — create a display to begin"
                : $"{displays.Count} active display{(displays.Count == 1 ? "" : "s")}";
            NotifyDisplayState();
        }
        catch (Exception ex)
        {
            StatusText = "Couldn't refresh — " + ex.Message;
            NotifyDisplayState();
        }
    }

    private async Task CreateDisplayAsync()
    {
        var dialog = new CreateDisplayDialog { Owner = Application.Current.MainWindow };
        if (dialog.ShowDialog() != true) return;
        try
        {
            var client = new HostClient();
            var display = await client.CreateDisplayAsync(dialog.Request);
            await client.SetAlwaysOnTopAsync(AppSettings.Load().KeepViewersOnTop);
            Add(display);
            StatusText = $"Created {display.Width}×{display.Height} display";
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Couldn't create display", MessageBoxButton.OK, MessageBoxImage.Warning);
            StatusText = "Create failed — " + ex.Message;
            await RefreshAsync();
        }
    }

    private async Task SaveLayoutAsync()
    {
        if (Displays.Count == 0)
        {
            MessageBox.Show("Create at least one display before saving a layout.", "MultiBox Viewer");
            return;
        }
        var dialog = new SaveLayoutDialog { Owner = Application.Current.MainWindow };
        if (dialog.ShowDialog() == true)
        {
            await new LayoutStore().SaveAsync(new LayoutInfo
            {
                Name = dialog.LayoutName,
                Displays = Displays.Select(x => x.Display).ToList()
            });
            StatusText = $"Saved layout “{dialog.LayoutName}”";
        }
    }

    private async Task RestoreLayoutAsync()
    {
        var dialog = new RestoreLayoutDialog { Owner = Application.Current.MainWindow };
        if (dialog.ShowDialog() != true || dialog.SelectedLayout is null) return;
        try
        {
            var client = new HostClient();
            var active = await client.ListDisplaysAsync();
            foreach (var display in active)
                await client.SendConnectorAsync(HostCommand.RemoveDisplay, display.ConnectorIndex);
            foreach (var display in dialog.SelectedLayout.Displays)
            {
                await client.CreateDisplayAsync(new CreateDisplayRequest
                {
                    Width = display.Width,
                    Height = display.Height,
                    RefreshHz = display.RefreshHz,
                    OpenViewer = true
                });
            }
            var settings = AppSettings.Load();
            await client.SetAlwaysOnTopAsync(settings.KeepViewersOnTop);
            await RefreshAsync();
            StatusText = $"Restored layout “{dialog.SelectedLayout.Name}”";
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Couldn't restore layout", MessageBoxButton.OK, MessageBoxImage.Warning);
            await RefreshAsync();
        }
    }

    private void Add(DisplayInfo display)
    {
        if (_customNames.TryGetValue(display.ConnectorIndex, out var customName) &&
            !string.IsNullOrWhiteSpace(customName))
        {
            display.Name = customName;
        }

        Displays.Add(new DisplayCardViewModel(
            display,
            card =>
            {
                _customNames.Remove(card.Display.ConnectorIndex);
                Displays.Remove(card);
                NotifyDisplayState();
            },
            Add,
            RefreshAsync));
        NotifyDisplayState();
    }

    private void NotifyDisplayState()
    {
        PropertyChanged?.Invoke(this, new(nameof(HasDisplays)));
        PropertyChanged?.Invoke(this, new(nameof(IsEmpty)));
    }
}
