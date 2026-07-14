# Native Windows Pen and Touch

This fork adds a Windows-only input path for integrated pen displays. It reads `WM_POINTER` pen history from the Moonlight stream window and forwards pressure, hover, eraser, barrel-button, and tilt information through Moonlight's existing pen protocol. No driver, service, virtual HID device, or additional network port is installed.

## Build prerequisites

- Windows 10 or Windows 11
- Visual Studio 2022 with Desktop development with C++ and a Windows 10/11 SDK
- Qt 6.7 or newer, MSVC 2022 64-bit
- Git, GitHub CLI, CMake, PowerShell 7, 7-Zip, and Gitleaks

Run `pwsh scripts/bootstrap.ps1 -Check`, followed by `pwsh scripts/test-windows-input.ps1` and `pwsh scripts/build-windows.ps1 -Configuration Debug`. The wrapper automatically works around the upstream batch script's inability to build from a path containing spaces.

## Client settings

Under Input Settings:

- Enable **Use native Windows pen input**.
- Leave **Pen and touch interaction** on **Windows default** initially.
- Enable privacy-safe diagnostics only while troubleshooting. Diagnostics contain aggregate sample counts only.

Restart an active stream after changing native pen settings.

## Host configuration

Enable native pen and touch input in Apollo. Start with a single virtual display at 2560×1440, 60 Hz, HEVC, and SDR. Match the client and virtual-display aspect ratio. After mapping and pressure are verified, test 3840×2160 at 60 Hz.

## Creative applications

- Krita: select the Windows Ink tablet input API.
- Photoshop: use its Windows Ink input path and test a pressure-sensitive brush.
- Substance 3D Painter: test pressure-sensitive size or flow and verify modifier release after changing focus.

## Hardware controls

The reliable public build path is to configure each tablet function key in the device control panel as a unique keyboard shortcut. Moonlight's existing keyboard path forwards those shortcuts. Wintab SDK source and binaries are not included because redistribution compatibility must be verified separately. The build wrapper reserves `-EnableLocalWintab` for a locally licensed SDK checkout via `WACOM_SDK_DIR`; it does not bundle Wacom components.

## Validation checklist

1. Unplug any external pen tablet during the first integrated-display test.
2. Verify hover, pressure from light to full, barrel button, eraser, and tilt.
3. Verify one- and two-finger touch while the pen is away from the display.
4. Draw into each corner at 1440p and 4K.
5. Change window size and fullscreen state and repeat the corner test.
6. Disconnect during a stroke and confirm that no button or modifier remains held.
7. Reconnect the external tablet only after the integrated display passes.

## Privacy

Do not attach real logs, captures, screenshots, device serial numbers, network addresses, computer names, or user-profile paths to public issues. Run `pwsh scripts/privacy-check.ps1 -Range upstream/master..HEAD` before pushing changes.

## Updating from upstream

Fetch `upstream`, review upstream changes, and rebase the feature branch onto a specific upstream commit. Rebuild and rerun the input tests before pushing. Never push directly to the upstream remote.
