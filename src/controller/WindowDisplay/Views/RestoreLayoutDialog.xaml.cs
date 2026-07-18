using System.Windows;
using WindowDisplay.Models;
using WindowDisplay.Services;

namespace WindowDisplay.Views;
public partial class RestoreLayoutDialog : Window
{
    public LayoutInfo? SelectedLayout => Layouts.SelectedItem as LayoutInfo;
    public RestoreLayoutDialog()
    {
        InitializeComponent();
        Loaded += async (_, _) => Layouts.ItemsSource = await new LayoutStore().LoadAllAsync();
    }
    private void Restore_Click(object sender, RoutedEventArgs e) { if (SelectedLayout is not null) DialogResult = true; }
}
