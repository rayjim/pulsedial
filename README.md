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
- Edge dock mode for auto-hiding at the left, right, or top screen edge
- AI coding quota mode for 5-hour and 7-day token allowance
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

## Publishing a GitHub Release

GitHub Actions builds and uploads the Windows x64 installer when a version tag is pushed.

```sh
git tag v0.2.0
git push origin v0.2.0
```

The workflow publishes:

```text
PulseDialSetup-x64.exe
```

## Prototype Controls

- Drag anywhere on the gadget to move it.
- Right-click opens the settings menu.
- Right-click the tray icon opens the same settings menu.
- Double-click the tray icon shows or hides the gadget.
- `Ctrl+Alt+P` toggles click-through, including when click-through is already enabled.
- When click-through is enabled, PulseDial shows a tray notification with the recovery shortcut.
- The window is topmost and hidden from the taskbar by default.

Settings currently available:

- Mode: Auto, System Dial, Quota Dial
- Always on top
- Enable / Disable click-through
- Compact mode
- Opacity: 60%, 80%, 100%
- Refresh rate: 250 ms, 500 ms, 1000 ms
- Edge dock
- Show / Hide
- Exit

## Quota Dial

Quota Dial shows token allowance progress instead of cost. It is designed for AI coding workflows where the useful question is "how much do I have left?"

PulseDial starts in `Auto` mode:

- If quota data is available, it shows Quota Dial.
- If quota data is missing, it falls back to the CPU / memory System Dial.

The first prototype reads local JSON data from:

```text
%LOCALAPPDATA%\PulseDial\quota.json
```

For quick testing, you can also place `quota.json` next to `PulseDial.exe`.

Example:

```json
{
  "source": "codex",
  "updatedAt": "2026-06-04T12:00:00Z",
  "windows": [
    {
      "label": "5 HOURS",
      "usedTokens": 420000,
      "remainingTokens": 580000,
      "resetIn": "03:42"
    },
    {
      "label": "7 DAYS",
      "usedTokens": 6800000,
      "remainingTokens": 3200000,
      "resetIn": "4d"
    }
  ]
}
```

## Edge Dock

Edge dock lets PulseDial tuck itself into the screen edge.

1. Enable `Edge dock` from the right-click or tray menu.
2. Drag PulseDial to the left, right, or top edge of the screen.
3. Release it near the edge.
4. PulseDial snaps to that edge and auto-hides after a short delay.
5. Move the mouse over the visible edge handle to show it again.

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
