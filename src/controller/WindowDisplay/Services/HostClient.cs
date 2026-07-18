using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using WindowDisplay.Models;
using WindowDisplay.Views;

namespace WindowDisplay.Services;

public enum HostCommand : uint
{
    Ping = 1, CreateDisplay, RemoveDisplay, OpenViewer, RestartDisplay, ListDisplays,
    MoveWindow, SetScaleMode, SetAlwaysOnTop, SaveLayout, RestoreLayout, RescueOffscreen,
    CreateDiagnostic = 13,
    SetDropWindows = 14,
    SetMirrorSource = 15,
    SetViewerInteraction = 16
}

public sealed class HostClient
{
    private const string PipeName = "WindowDisplay.Host";
    private const int MonitorRuntimeSize = 360;
    private const int MaxMonitors = 8;

    public async Task EnsureHostAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            await PingAsync(cancellationToken);
        }
        catch
        {
            StartHost(elevated: true);
            await PingWithRetryAsync(cancellationToken);
        }
    }

    public async Task PingAsync(CancellationToken cancellationToken = default)
    {
        _ = await SendRawAsync(HostCommand.Ping, [], cancellationToken);
    }

    public async Task<DisplayInfo> CreateDisplayAsync(
        CreateDisplayRequest request,
        CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[16];
        WriteUInt32(payload, 0, checked((uint)request.Width));
        WriteUInt32(payload, 4, checked((uint)request.Height));
        WriteUInt32(payload, 8, checked((uint)request.RefreshHz));
        WriteUInt32(payload, 12, request.OpenViewer ? 1u : 0u);
        var response = await SendRawAsync(HostCommand.CreateDisplay, payload, cancellationToken);
        return ParseMonitor(response, 0);
    }

    public async Task SendConnectorAsync(
        HostCommand command,
        int connectorIndex,
        CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[4];
        WriteUInt32(payload, 0, checked((uint)connectorIndex));
        await SendRawAsync(command, payload, cancellationToken);
    }

    public async Task MoveWindowAsync(
        nint window,
        int connectorIndex,
        CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[12];
        BitConverter.TryWriteBytes(payload.AsSpan(0, sizeof(ulong)), unchecked((ulong)window.ToInt64()));
        WriteUInt32(payload, 8, checked((uint)connectorIndex));
        await SendRawAsync(HostCommand.MoveWindow, payload, cancellationToken);
    }

    public async Task SetAlwaysOnTopAsync(bool enabled, CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[4];
        WriteUInt32(payload, 0, enabled ? 1u : 0u);
        await SendRawAsync(HostCommand.SetAlwaysOnTop, payload, cancellationToken);
    }

    public async Task SetScaleModeAsync(int connectorIndex, uint scaleMode, CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[8];
        WriteUInt32(payload, 0, checked((uint)connectorIndex));
        WriteUInt32(payload, 4, scaleMode);
        await SendRawAsync(HostCommand.SetScaleMode, payload, cancellationToken);
    }

    public async Task RescueOffscreenAsync(CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        await SendRawAsync(HostCommand.RescueOffscreen, [], cancellationToken);
    }

    public async Task SetDropWindowsAsync(bool enabled, CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[4];
        WriteUInt32(payload, 0, enabled ? 1u : 0u);
        await SendRawAsync(HostCommand.SetDropWindows, payload, cancellationToken);
    }

    public async Task SetViewerInteractionAsync(
        bool requireDoubleClick,
        bool hoverFade,
        CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[8];
        WriteUInt32(payload, 0, requireDoubleClick ? 1u : 0u);
        WriteUInt32(payload, 4, hoverFade ? 1u : 0u);
        await SendRawAsync(HostCommand.SetViewerInteraction, payload, cancellationToken);
    }

    public async Task SetMirrorSourceAsync(
        int connectorIndex,
        string? gdiDeviceName,
        CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var payload = new byte[4 + 128 * 2];
        WriteUInt32(payload, 0, checked((uint)connectorIndex));
        if (!string.IsNullOrWhiteSpace(gdiDeviceName))
        {
            var clipped = gdiDeviceName.Length > 127 ? gdiDeviceName[..127] : gdiDeviceName;
            var encoded = Encoding.Unicode.GetBytes(clipped);
            Buffer.BlockCopy(encoded, 0, payload, 4, Math.Min(encoded.Length, 127 * 2));
        }
        await SendRawAsync(HostCommand.SetMirrorSource, payload, cancellationToken);
    }

    public async Task<List<DisplayInfo>> ListDisplaysAsync(CancellationToken cancellationToken = default)
    {
        await EnsureHostAsync(cancellationToken);
        var response = await SendRawAsync(HostCommand.ListDisplays, [], cancellationToken);
        if (response.Length < 8)
            throw new InvalidDataException("Host returned an incomplete display list.");

        var monitorCount = checked((int)Math.Min(ReadUInt32(response, 4), MaxMonitors));
        var required = checked(8 + monitorCount * MonitorRuntimeSize);
        if (response.Length < required)
            throw new InvalidDataException("Host returned a truncated display list.");

        var displays = new List<DisplayInfo>(monitorCount);
        for (var i = 0; i < monitorCount; i++)
        {
            var offset = 8 + i * MonitorRuntimeSize;
            if (ReadUInt32(response, offset + 4) != 0)
                displays.Add(ParseMonitor(response, offset));
        }
        return displays;
    }

    private static async Task PingWithRetryAsync(CancellationToken cancellationToken)
    {
        Exception? last = null;
        for (var attempt = 0; attempt < 40; attempt++)
        {
            try
            {
                await new HostClient().PingAsync(cancellationToken);
                return;
            }
            catch (Exception ex)
            {
                last = ex;
                await Task.Delay(250, cancellationToken);
            }
        }
        throw new InvalidOperationException(
            "Could not connect to MultiBox Viewer host. Approve the UAC prompt if shown, then try again.",
            last);
    }

    private static async Task<byte[]> SendRawAsync(
        HostCommand command,
        byte[] payload,
        CancellationToken cancellationToken)
    {
        await using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
        try { await pipe.ConnectAsync(800, cancellationToken); }
        catch (TimeoutException)
        {
            StartHost(elevated: true);
            await pipe.ConnectAsync(12_000, cancellationToken);
        }

        var requestId = unchecked((uint)Environment.TickCount);
        var header = new byte[12];
        WriteUInt32(header, 0, (uint)command);
        WriteUInt32(header, 4, checked((uint)payload.Length));
        WriteUInt32(header, 8, requestId);
        await pipe.WriteAsync(header, cancellationToken);
        if (payload.Length != 0)
            await pipe.WriteAsync(payload, cancellationToken);
        await pipe.FlushAsync(cancellationToken);

        await ReadExactlyAsync(pipe, header, cancellationToken);
        var responseId = ReadUInt32(header, 0);
        var status = ReadUInt32(header, 4);
        var length = ReadUInt32(header, 8);
        if (responseId != requestId)
            throw new InvalidDataException("Host response did not match the request.");
        if (status != 0)
            throw new InvalidOperationException($"Host returned Win32 error {status}.");
        if (length > 1024 * 1024)
            throw new InvalidDataException("Host response was too large.");

        var result = new byte[checked((int)length)];
        await ReadExactlyAsync(pipe, result, cancellationToken);
        return result;
    }

    private static DisplayInfo ParseMonitor(byte[] bytes, int offset)
    {
        if (offset < 0 || bytes.Length - offset < MonitorRuntimeSize)
            throw new InvalidDataException("Host returned an incomplete monitor record.");

        var stateValue = ReadUInt32(bytes, offset + 8);
        var name = ReadFixedUnicode(bytes, offset + 72, 64);
        return new DisplayInfo
        {
            ConnectorIndex = checked((int)ReadUInt32(bytes, offset)),
            Name = string.IsNullOrWhiteSpace(name) ? "MultiBox Viewer" : name,
            Width = checked((int)ReadUInt32(bytes, offset + 12)),
            Height = checked((int)ReadUInt32(bytes, offset + 16)),
            RefreshHz = checked((int)ReadUInt32(bytes, offset + 20)),
            State = Enum.IsDefined(typeof(DisplayState), (int)stateValue)
                ? (DisplayState)stateValue
                : DisplayState.NeedsAttention
        };
    }

    private static string ReadFixedUnicode(byte[] bytes, int offset, int characters)
    {
        var value = Encoding.Unicode.GetString(bytes, offset, characters * 2);
        var terminator = value.IndexOf('\0');
        return terminator >= 0 ? value[..terminator] : value;
    }

    private static uint ReadUInt32(byte[] bytes, int offset) =>
        BitConverter.ToUInt32(bytes, offset);

    private static void WriteUInt32(byte[] bytes, int offset, uint value) =>
        BitConverter.TryWriteBytes(bytes.AsSpan(offset, sizeof(uint)), value);

    private static async Task ReadExactlyAsync(
        Stream stream,
        byte[] buffer,
        CancellationToken cancellationToken)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = await stream.ReadAsync(buffer.AsMemory(offset), cancellationToken);
            if (read == 0)
                throw new EndOfStreamException("Host closed the pipe before completing its response.");
            offset += read;
        }
    }

    public static void StartHost(bool elevated = true)
    {
        if (Process.GetProcessesByName("WindowDisplayHost").Length > 0)
            return;

        var host = FindHostExecutable()
            ?? throw new FileNotFoundException("WindowDisplayHost.exe was not found next to the app or in artifacts/Release.");

        var start = new ProcessStartInfo(host)
        {
            UseShellExecute = true,
            Arguments = "--elevated"
        };
        if (elevated)
            start.Verb = "runas";
        try
        {
            Process.Start(start);
        }
        catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            throw new InvalidOperationException("Starting the MultiBox Viewer host requires Administrator approval (UAC).", ex);
        }
    }

    public static string? FindHostExecutable()
    {
        var candidates = new[]
        {
            Path.Combine(AppContext.BaseDirectory, "WindowDisplayHost.exe"),
            Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "..", "artifacts", "Release", "WindowDisplayHost.exe")),
            Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "..", "src", "host", "WindowDisplayHost", "bin", "x64", "Release", "WindowDisplayHost.exe")),
            Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "..", "bin", "x64", "Release", "WindowDisplayHost.exe"))
        };
        return candidates.FirstOrDefault(File.Exists);
    }
}
