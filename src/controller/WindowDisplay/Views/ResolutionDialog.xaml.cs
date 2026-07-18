using System.Windows;
using System.Windows.Controls;

namespace WindowDisplay.Views;

public partial class ResolutionDialog : Window
{
    public CreateDisplayRequest? Request { get; private set; }

    public ResolutionDialog(int width, int height, int refreshHz)
    {
        InitializeComponent();
        for (var i = 0; i < PresetList.Items.Count; i++)
        {
            if (PresetList.Items[i] is ListBoxItem item &&
                item.Tag?.ToString() == $"{width},{height},{refreshHz}")
            {
                PresetList.SelectedIndex = i;
                break;
            }
        }
    }

    private void Apply_Click(object sender, RoutedEventArgs e)
    {
        var tag = ((ListBoxItem)PresetList.SelectedItem).Tag?.ToString();
        if (string.IsNullOrWhiteSpace(tag)) return;
        var values = tag.Split(',').Select(int.Parse).ToArray();
        Request = new CreateDisplayRequest
        {
            Width = values[0],
            Height = values[1],
            RefreshHz = values[2],
            OpenViewer = true
        };
        DialogResult = true;
    }
}
