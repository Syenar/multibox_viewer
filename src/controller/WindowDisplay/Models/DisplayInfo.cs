namespace WindowDisplay.Models;

public sealed class DisplayInfo
{
    public int ConnectorIndex { get; set; }
    public string Name { get; set; } = "MultiBox Viewer";
    public int Width { get; set; } = 1920;
    public int Height { get; set; } = 1080;
    public int RefreshHz { get; set; } = 60;
    public DisplayState State { get; set; } = DisplayState.Active;
    public string Orientation { get; set; } = "Landscape";
    public string ResolutionLabel => $"{Width} × {Height} · {RefreshHz} Hz";
}
