namespace WindowDisplay.Models;

public sealed class AppWindowInfo
{
    public nint Handle { get; init; }
    public string Title { get; init; } = string.Empty;
    public string ProcessName { get; init; } = string.Empty;
    public string DisplayName => string.IsNullOrWhiteSpace(ProcessName) ? Title : $"{Title} — {ProcessName}";
}
