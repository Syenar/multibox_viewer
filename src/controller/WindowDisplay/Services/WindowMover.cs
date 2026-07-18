using WindowDisplay.Models;

namespace WindowDisplay.Services;

public sealed class WindowMover
{
    public Task MoveToDisplayAsync(AppWindowInfo app, DisplayInfo display) =>
        new HostClient().MoveWindowAsync(app.Handle, display.ConnectorIndex);
}
