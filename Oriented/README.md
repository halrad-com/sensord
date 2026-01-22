# Oriented

A lightweight Windows utility for controlling screen orientation, designed for tablets and convertible devices.

## Implementations

| Version | Size | Status |
|---------|------|--------|
| [CPP](CPP/) | ~500KB | **Active** |
| C# (WinUI 3) | 205MB | Abandoned |
| C# (WPF) | 24MB | Abandoned |

### Why C++?

The C# implementations were abandoned due to deployment size issues:

**WinUI 3** - Required 205MB for a trivial ~500 line app. The WinUI 3 runtime and dependencies are massive, and self-contained deployment bundles the entire framework.

**WPF** - Better at 24MB with PublishSingleFile, but still ~50x larger than necessary. Framework-dependent deployment reduces size but requires .NET runtime pre-installed on target devices, which isn't guaranteed on tablets.

**C++ (Win32)** - Native executable with no runtime dependencies. Uses WinRT C++ headers for sensor access (built into Windows 10+). Single 500KB file that runs on any Windows 10+ device out of the box. This is the correct tool for the job.

The C# sample is archived in `Oriented_CSharp.rar` - a minimal proof-of-concept that was quickly abandoned once the absurd deployment sizes became apparent.

## Features

- **Double-tap to rotate** - Double-click or double-tap the window to rotate the screen 90°
- **Drag to move** - Click and drag anywhere on the window to reposition it
- **Global hotkeys** - Ctrl+Alt+Arrow keys for instant rotation (NVIDIA/Intel style)
- **System tray integration** - Runs in system tray for unobtrusive operation
- **Lock/unlock rotation** - Click the tray icon to toggle between locked and auto-rotate modes
- **Sensor support** - Automatically rotates based on device orientation when unlocked
- **Single file** - ~500KB native Windows executable with all resources embedded

## Quick Start

Download `Oriented.exe` from [CPP/](CPP/) and run it. No installation required.

See [CPP/README.md](CPP/README.md) for full usage and build instructions.

## License

MIT License - see [CPP/LICENSE](CPP/LICENSE)
