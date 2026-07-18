using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows;
using WindowDisplay.Helpers;
using WindowDisplay.Models;
using WindowDisplay.Services;
using WindowDisplay.Views;

namespace WindowDisplay.ViewModels;

public sealed class DisplayCardViewModel(
    DisplayInfo display,
    Action<DisplayCardViewModel> removed,
    Action<DisplayInfo> created,
    Func<Task>? refreshAll = null) : INotifyPropertyChanged
{
    public DisplayInfo Display { get; } = display;
    public string Name { get => Display.Name; set { Display.Name = value; OnChanged(); OnChanged(nameof(Title)); } }
    public string Title => Display.Name;
    public string Resolution => Display.ResolutionLabel;
    public string StateText => Display.State == DisplayState.NeedsAttention ? "Needs Attention" : "Active";
    public bool NeedsAttention => Display.State == DisplayState.NeedsAttention;
    public RelayCommand RenameCommand => new(_ => Rename());
    public RelayCommand OpenViewerCommand => new(async _ => await CallAsync(HostCommand.OpenViewer));
    public RelayCommand MoveAppCommand => new(_ => MoveApp());
    public RelayCommand RestartCommand => new(async _ => await CallAsync(HostCommand.RestartDisplay));
    public RelayCommand RepairCommand => new(async _ => await RepairAsync());
    public RelayCommand RemoveCommand => new(async _ => await RemoveAsync());
    public RelayCommand ChangeResolutionCommand => new(async _ => await ChangeResolutionAsync());
    public RelayCommand ChangeOrientationCommand => new(_ =>
    {
        // Same path as a physical monitor — Windows owns orientation.
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(
            "ms-settings:display")
        { UseShellExecute = true });
    });
    public RelayCommand DuplicateCommand => new(async _ => await DuplicateAsync());
    public RelayCommand OpenSettingsCommand => new(_ => System.Diagnostics.Process.Start(
        new System.Diagnostics.ProcessStartInfo("ms-settings:display") { UseShellExecute = true }));
    public event PropertyChangedEventHandler? PropertyChanged;

    private async Task<bool> CallAsync(HostCommand command)
    {
        try
        {
            await new HostClient().SendConnectorAsync(command, Display.ConnectorIndex);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "MultiBox Viewer", MessageBoxButton.OK, MessageBoxImage.Warning);
            return false;
        }
    }

    private void Rename()
    {
        var dialog = new RenameDialog(Name) { Owner = Application.Current.MainWindow };
        if (dialog.ShowDialog() == true) Name = dialog.DisplayName;
    }

    private void MoveApp()
    {
        var dialog = new AppPickerDialog(Display) { Owner = Application.Current.MainWindow };
        dialog.ShowDialog();
    }

    private async Task RepairAsync()
    {
        var result = await new RepairService().RunAsync(Display.ConnectorIndex);
        MessageBox.Show(result.Summary + Environment.NewLine + string.Join(Environment.NewLine, result.Steps), "Repair display");
        if (result.IsHealthy && refreshAll is not null)
            await refreshAll();
    }

    private async Task RemoveAsync()
    {
        if (MessageBox.Show($"Remove {Name}? Its viewer will close.", "Remove display",
                MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
        if (await CallAsync(HostCommand.RemoveDisplay))
            removed(this);
    }

    private async Task ChangeResolutionAsync()
    {
        var dialog = new ResolutionDialog(Display.Width, Display.Height, Display.RefreshHz)
        {
            Owner = Application.Current.MainWindow
        };
        if (dialog.ShowDialog() != true || dialog.Request is null) return;
        try
        {
            var client = new HostClient();
            await client.SendConnectorAsync(HostCommand.RemoveDisplay, Display.ConnectorIndex);
            var replacement = await client.CreateDisplayAsync(dialog.Request);
            removed(this);
            created(replacement);
            if (refreshAll is not null) await refreshAll();
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "Couldn't change resolution", MessageBoxButton.OK, MessageBoxImage.Warning);
            if (refreshAll is not null) await refreshAll();
        }
    }

    private async Task DuplicateAsync()
    {
        try
        {
            var duplicate = await new HostClient().CreateDisplayAsync(new CreateDisplayRequest
            {
                Width = Display.Width,
                Height = Display.Height,
                RefreshHz = Display.RefreshHz,
                OpenViewer = true
            });
            created(duplicate);
        }
        catch (Exception ex)
        {
            MessageBox.Show(ex.Message, "MultiBox Viewer", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void OnChanged([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
}
