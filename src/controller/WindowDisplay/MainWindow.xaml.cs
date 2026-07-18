using System.Windows;
using WindowDisplay.ViewModels;

namespace WindowDisplay;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Loaded += async (_, _) => await ((MainViewModel)DataContext).InitializeAsync();
    }
}
