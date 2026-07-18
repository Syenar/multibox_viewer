namespace WindowDisplay.Services;

public sealed record RepairResult(bool IsHealthy, string Summary, IReadOnlyList<string> Steps);

public sealed class RepairService
{
    public async Task<RepairResult> RunAsync(int? connectorIndex = null)
    {
        var steps = new List<string>();
        var client = new HostClient();
        try
        {
            await client.EnsureHostAsync();
            steps.Add("Host is running and reachable.");

            if (connectorIndex is int connector)
            {
                await client.SendConnectorAsync(HostCommand.RestartDisplay, connector);
                steps.Add($"Restarted display connector {connector}.");
                await client.SendConnectorAsync(HostCommand.OpenViewer, connector);
                steps.Add("Reopened the picture-in-picture viewer.");
            }

            await client.RescueOffscreenAsync();
            steps.Add("Rescued any off-screen windows.");

            var settings = AppSettings.Load();
            await client.SetAlwaysOnTopAsync(settings.KeepViewersOnTop);
            steps.Add(settings.KeepViewersOnTop
                ? "Viewers set to stay on top."
                : "Viewer always-on-top preference applied.");

            return new RepairResult(true, "Repair completed successfully.", steps);
        }
        catch (Exception ex)
        {
            steps.Add("Approve UAC if prompted so WindowDisplayHost.exe can start elevated.");
            steps.Add("Confirm the MultiBox Viewer virtual display driver is installed (tools\\sign-and-install-driver.ps1).");
            steps.Add("Use Windows Display Settings → Detect, then retry Repair.");
            return new RepairResult(false, $"Repair could not finish: {ex.Message}", steps);
        }
    }
}
