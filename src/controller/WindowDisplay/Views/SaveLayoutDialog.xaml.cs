using System.Windows;
namespace WindowDisplay.Views;
public partial class SaveLayoutDialog : Window
{
    public string LayoutName => NameBox.Text.Trim();
    public SaveLayoutDialog() => InitializeComponent();
    private void Save_Click(object sender, RoutedEventArgs e) { if (!string.IsNullOrWhiteSpace(LayoutName)) DialogResult = true; }
}
