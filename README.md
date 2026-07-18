# MultiBox Viewer

MultiBox Viewer is a Windows 11 application for creating real virtual displays and opening each display inside its own movable, resizable viewer window.

The app allows users to create additional monitors recognized by Windows, move applications onto them, interact with those applications through local viewer windows, and save complete multi-display layouts for later use.

MultiBox Viewer is designed around simplicity. Creating a new display, moving an app, restoring a workspace, or removing a display should take no more than three to five steps.

## Planned features

- Create real virtual monitors recognized by Windows
- Open each virtual display in a movable and resizable window
- Create multiple independent virtual displays
- Move running applications to a selected display
- Mouse, keyboard, and scroll input through each viewer
- Fit, fill, actual-size, and full-screen viewing modes
- Resolution and orientation controls
- Automatic display arrangement
- Save and restore complete display layouts
- Restore windows safely when a display is removed
- Recover from sleep, reboot, and display-driver restarts
- Light and dark themes
- Integrated driver and application installer

## Technology

MultiBox Viewer is planned as a native Windows project using:

- C++ and the Windows Driver Kit
- Microsoft IddCx indirect display drivers
- Direct3D for display rendering
- C# and .NET for the desktop interface
- Win32 display and window-management APIs

## Project status

MultiBox Viewer is currently in the planning and early development stage. The first milestone is a minimal prototype capable of creating one real virtual monitor, opening it in a local viewer, forwarding input, and removing it safely.
