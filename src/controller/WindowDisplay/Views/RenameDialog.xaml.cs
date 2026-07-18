using System.Windows;
namespace WindowDisplay.Views;
public partial class RenameDialog : Window
{
    public string DisplayName => NameBox.Text.Trim();
    public RenameDialog(string currentName) { InitializeComponent(); NameBox.Text = currentName; NameBox.SelectAll(); }
    private void Rename_Click(object sender, RoutedEventArgs e) { if (!string.IsNullOrWhiteSpace(DisplayName)) DialogResult = true; }
}
