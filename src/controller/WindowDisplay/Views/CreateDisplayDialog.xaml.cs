using System.Windows;
using System.Windows.Controls;

namespace WindowDisplay.Views;

public sealed class CreateDisplayRequest
{
    public int Width { get; init; } = 1920;
    public int Height { get; init; } = 1080;
    public int RefreshHz { get; init; } = 60;
    public bool OpenViewer { get; init; } = true;
}

public partial class CreateDisplayDialog : Window
{
    public CreateDisplayRequest Request { get; private set; } = new();
    public CreateDisplayDialog() => InitializeComponent();

    private void PresetList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (CustomPanel is null || PresetList.SelectedItem is not ListBoxItem item) return;
        CustomPanel.Visibility = item.Tag?.ToString() == "custom" ? Visibility.Visible : Visibility.Collapsed;
    }

    private void Create_Click(object sender, RoutedEventArgs e)
    {
        var tag = ((ListBoxItem)PresetList.SelectedItem).Tag?.ToString();
        int width, height, refresh;
        if (tag == "custom")
        {
            if (!int.TryParse(CustomWidth.Text.Trim(), out width) ||
                !int.TryParse(CustomHeight.Text.Trim(), out height) ||
                !int.TryParse(CustomRefresh.Text.Trim(), out refresh) ||
                width < 640 || height < 480 || refresh < 30 ||
                width > 3840 || height > 2160 || refresh > 240)
            {
                MessageBox.Show("Enter a valid custom mode up to 3840 × 2160 @ 30–240 Hz.", "Custom display");
                return;
            }
        }
        else
        {
            var values = tag!.Split(',').Select(int.Parse).ToArray();
            width = values[0];
            height = values[1];
            refresh = values[2];
        }

        Request = new CreateDisplayRequest
        {
            Width = width,
            Height = height,
            RefreshHz = refresh,
            OpenViewer = OpenViewer.IsChecked == true
        };
        DialogResult = true;
    }
}
