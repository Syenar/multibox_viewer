namespace WindowDisplay.Models;

public sealed class LayoutInfo
{
    public string Name { get; set; } = "Untitled layout";
    public DateTime SavedAt { get; set; } = DateTime.Now;
    public List<DisplayInfo> Displays { get; set; } = [];
}
