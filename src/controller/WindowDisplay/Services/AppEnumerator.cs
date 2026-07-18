using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using WindowDisplay.Models;

namespace WindowDisplay.Services;

public sealed class AppEnumerator
{
    private delegate bool EnumWindowsProc(nint hWnd, nint lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, nint lParam);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(nint hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(nint hWnd, StringBuilder text, int maxCount);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(nint hWnd, out uint processId);

    public IReadOnlyList<AppWindowInfo> GetUserApps()
    {
        var windows = new List<AppWindowInfo>();
        EnumWindows((handle, _) =>
        {
            if (!IsWindowVisible(handle)) return true;
            var title = new StringBuilder(512);
            if (GetWindowText(handle, title, title.Capacity) == 0) return true;
            GetWindowThreadProcessId(handle, out var processId);
            try
            {
                var process = Process.GetProcessById((int)processId);
                var name = process.ProcessName;
                if (name is "explorer" or "ApplicationFrameHost" || name.Contains("WindowDisplay", StringComparison.OrdinalIgnoreCase)) return true;
                windows.Add(new AppWindowInfo { Handle = handle, Title = title.ToString(), ProcessName = name });
            }
            catch (ArgumentException) { }
            return true;
        }, 0);
        return windows.OrderBy(x => x.Title).ToList();
    }
}
