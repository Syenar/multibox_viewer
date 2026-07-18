# MultiBox Viewer

MultiBox Viewer turns extra monitors into picture-in-picture windows on Windows 11.

It creates real virtual displays that Windows treats like normal monitors, then shows each one inside its own floating, movable, resizable viewer — like picture-in-picture for your whole desktop. Move apps onto those displays, work in them through the viewer windows, and save the full layout for later.

Creating a new display, moving an app, restoring a workspace, or removing a display should take no more than three to five steps.

## Planned features

- Create real virtual monitors recognized by Windows
- View each display in a picture-in-picture style floating window
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
