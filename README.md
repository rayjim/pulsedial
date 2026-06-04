# PulseDial

PulseDial is a lightweight Windows native CPU and memory desktop gadget prototype.

It is intentionally small:

- Win32 borderless floating window
- Direct2D rendering
- DirectWrite text
- CPU usage from `GetSystemTimes`
- Memory usage from `GlobalMemoryStatusEx`
- 60-second in-memory history
- Always-on-top, opacity, compact mode, and click-through controls
- Tray icon with menu access
- No tray service, no persistent storage, no web runtime

## Build

Requirements:

- Windows 10/11
- Visual Studio 2022 with C++ desktop workload
- CMake 3.21+

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\PulseDial.exe
```

From Linux/WSL with `mingw-w64`:

```sh
./scripts/build-mingw.sh
```

The generated executable is written to:

```text
build/PulseDial.exe
```

## Windows x64 Installer

From Linux/WSL with `mingw-w64` and NSIS:

```sh
./scripts/build-release.sh
```

The generated installer is written to:

```text
dist/PulseDialSetup-x64.exe
```

The installer:

- Installs `PulseDial.exe` to `%LOCALAPPDATA%\Programs\PulseDial`.
- Creates Start Menu shortcuts.
- Registers PulseDial in Windows Apps & Features for uninstall.
- Enables start-at-login by default through the current user's `Run` registry key.
- Removes the start-at-login entry during uninstall.
- Does not require administrator privileges.

## Prototype Controls

- Drag anywhere on the gadget to move it.
- Right-click opens the settings menu.
- Right-click the tray icon opens the same settings menu.
- Double-click the tray icon shows or hides the gadget.
- `Ctrl+Alt+P` toggles click-through, including when click-through is already enabled.
- When click-through is enabled, PulseDial shows a tray notification with the recovery shortcut.
- The window is topmost and hidden from the taskbar by default.

Settings currently available:

- Always on top
- Enable / Disable click-through
- Compact mode
- Opacity: 60%, 80%, 100%
- Refresh rate: 250 ms, 500 ms, 1000 ms
- Show / Hide
- Exit

## Visual Direction

The first version follows the optimized concept:

- CPU as the primary half-dial gauge
- Memory as a segmented mechanical slot
- CPU and memory history as compact oscilloscope-style lines
- Calm dark metal palette with cyan CPU, amber memory, and red overload state

## Next Implementation Pass

Good next steps:

- Persist settings.
- Add autostart option.
- Add a startup position picker.
