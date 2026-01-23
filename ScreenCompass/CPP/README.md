# HALRAD ScreenCompass - Always the right angle.

A lightweight Windows utility for managing screen orientation, built for tablets and convertible devices.

![ScreenCompass App](Halrad-sensord-ScreenCompass.png)

## Features

* **Double-tap to rotate** — Double-click or double-tap the window to rotate the display 90°.
* **Drag to move** — Click and drag anywhere on the window to reposition it.
* **Global hotkeys** — Use Ctrl + Alt + Arrow keys for instant rotation (NVIDIA/Intel style).
* **Multi-monitor support** — Hotkeys rotate whichever monitor the cursor is on.
* **System tray integration** — Runs quietly in the tray for unobtrusive operation.
* **Lock / unlock rotation** — Click the tray icon to toggle between locked and auto-rotate modes.
* **Sensor support** — Automatically rotates based on device orientation when unlocked.
* **Single-file executable** — ~500 KB native Windows binary with all resources embedded.

## Usage

### Command Line

```
ScreenCompass.exe [-m]
```

- `-m` or `/m` or `-minimized` - Start minimized to system tray

### Main Window

* **Click + drag** — Move the window.
* **Double-click / tap** — Rotate the screen 90° clockwise.
* **Right-click** — Open the context menu.
* **Close (X)** — Minimize to the system tray.
* **Shift + Close (X)** — Actually close the application.

### System Tray

* **Left-click** — Toggle rotation lock.
  * **Green icon** — Auto-rotation enabled (unlocked).
  * **Orange icon** — Rotation locked.
* **Right-click menu**:
  * **Show** — Restore the main window.
  * **Toggle 90°** — Rotate the screen 90° clockwise.
  * **Auto** — Orient to the current sensor reading.
  * **Exit** — Close the application.

### Global Hotkeys

NVIDIA/Intel-style shortcuts work system-wide. On multi-monitor setups, the monitor under the cursor is rotated.

| Hotkey         | Action                    |
| -------------- | ------------------------- |
| Ctrl+Alt+Up    | Normal (0°)               |
| Ctrl+Alt+Down  | Upside-down (180°)        |
| Ctrl+Alt+Left  | Portrait - top at left    |
| Ctrl+Alt+Right | Portrait - top at right   |

## Building

Requires **Visual Studio 2022** with the C++ workload and Windows SDK.

```powershell
# Convert icons (only needed if modifying source images)
.\convert-icon.ps1

# Build
.\build.ps1
```

## Deployment

Copy `ScreenCompass.exe` to the target device. All resources (icons, background image, manifest) are embedded in the executable.

## Debugging

ScreenCompass outputs diagnostic messages via `OutputDebugString`. View them with:

- **DebugView** (Sysinternals) - Run DebugView, enable "Capture Win32", then run ScreenCompass
- **Visual Studio** - Run with debugger attached (F5), messages appear in Output window

Messages are prefixed with `[ScreenCompass]` and tagged by level:

| Level | Meaning |
|-------|---------|
| INFO | Key events (startup, shutdown, sensor found, lock/unlock) |
| WARN | Non-fatal issues (hotkey conflicts, display change failed) |
| ERROR | Failures (GDI+ init, window creation) |
| DEBUG | Diagnostic detail (orientation changes) |

Example output:
```
[ScreenCompass] INFO: Starting ScreenCompass
[ScreenCompass] INFO: Orientation sensor found
[ScreenCompass] WARN: Some hotkeys failed to register (may be claimed by GPU driver)
[ScreenCompass] INFO: Rotation unlocked (auto)
[ScreenCompass] DEBUG: Sensor orientation change
```

## Requirements

* Windows 10 or later
* Orientation sensor (optional — manual rotation works without one)

## License

MIT License - see [LICENSE](LICENSE)
