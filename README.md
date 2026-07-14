# Moonlight PC — Native Windows Pen and Touch Fork

This experimental fork of [Moonlight PC](https://github.com/moonlight-stream/moonlight-qt) adds a Windows-native pen input path for pen-display clients. It captures Windows Pointer API pen events in the Moonlight streaming window and forwards them through Moonlight's existing pen protocol to a compatible Apollo or Sunshine host.

The feature is intended for creative applications that use Windows Ink, including Krita, Adobe Photoshop, and Substance 3D Painter. It adds no network protocol, port, driver, virtual device, service, or background process.

> [!IMPORTANT]
> This branch builds and its automated Windows-input tests pass, but it has not completed physical-device acceptance testing. Treat it as an experimental development build. See [Project status](#project-status) and [Known limitations](#known-limitations) before installing it.

## Project status

| Area | Status | Notes |
| --- | --- | --- |
| Native `WM_POINTER` pen capture | Implemented | Windows-only; active while streaming to a host that advertises pen/touch support. |
| Pressure, hover, eraser, barrel button, and tilt | Implemented | Forwarded through Moonlight's existing pen messages. |
| Coalesced pen history | Implemented | Samples are processed in chronological order. |
| Letterbox-aware coordinate mapping | Implemented | New contact outside the video area is ignored; active strokes are clamped. |
| Stuck-state cleanup | Implemented | Pen state is cancelled on capture loss and stream teardown. |
| Finger touch | Preserved | Uses Moonlight's existing SDL touch path with an added pen/touch policy. |
| Duplicate SDL pen suppression | Implemented | Native pen input is authoritative when enabled; finger touch and real mouse input remain enabled. |
| QuickKey remapping | Implemented | Configure tablet buttons as F13–F24, then optionally remap them in Moonlight. |
| QuickKey profiles | Implemented | Independent Default, Krita, Photoshop, and Substance 3D Painter mapping containers. |
| Aggregate diagnostics | Implemented | Optional sample and dropped-event counts; detailed live diagnostics are not yet implemented. |
| Automated Windows-input tests | Passing | Covers the current geometry and tilt-conversion components. |
| Debug and Release Windows builds | Passing | Portable packaging has also been exercised locally. |
| Physical tablet and application validation | Pending | Pressure, touch, tilt, and corner accuracy still require real-device testing. |
| Wintab control discovery | Not implemented | No Wacom SDK files or `Wintab32.dll` are bundled. |
| Public binary release | Not available | Build from source. Do not download binaries offered by unrelated third parties. |

## Features

### Native Windows pen input

When **Use native Windows pen input** is enabled, the stream window handles:

- Pen enter, hover, contact, movement, release, and leave events
- Pressure normalized from the Windows Pointer API range
- Pen and eraser tools, including inverted-pen eraser state
- Primary barrel-button state
- X/Y tilt converted to the polar representation used by the host
- Coalesced history samples for smoother high-frequency strokes
- Cancellation during capture loss or stream shutdown to reduce stuck buttons and strokes

Only `PT_PEN` pointer messages use this path. `PT_TOUCH` remains on Moonlight's established touch path.

### Pen-aware display mapping

Pen coordinates are normalized against the active stream aspect ratio rather than the entire window:

- Letterbox and pillarbox areas are excluded.
- A new stroke beginning outside the video area is ignored.
- A stroke that began inside the video area is clamped when it crosses the edge.
- Hover leaving the active video area is sent as a pen leave.

Physical corner accuracy at each target resolution must still be validated on real hardware.

### Touch policies

The Input Settings page provides three policies:

- **Windows default** — preserves the touch and palm-rejection behavior supplied by Windows and the tablet driver. This is the recommended starting point.
- **Disable touch while pen is in range** — prevents new finger contacts while the pen is detected near the display. Use this if palm touches reach the host unexpectedly.
- **Always forward touch** — forwards the existing Moonlight touch path even while the pen is in range.

### QuickKey profiles

The public, driver-independent control path uses keyboard chords:

1. Configure the tablet's physical buttons in its HP/Wacom control panel as F13 through F24.
2. Moonlight receives those keys through its normal keyboard input path.
3. Each slot may pass through unchanged or be remapped to one shortcut.

Profiles are available for:

- Default
- Krita
- Photoshop
- Substance 3D Painter

Profiles are independent mapping containers; they do not currently contain opinionated application presets. Empty slots pass F13–F24 through to the host unchanged.

Supported remap targets include letters, numbers, F1–F24, arrow and navigation keys, Space, Tab, Enter, Escape, Backspace, Insert, and Delete. Ctrl, Alt, Shift, and Meta/Windows modifiers are supported. Multi-step key sequences and arbitrary text macros are not supported.

## Requirements

### Tablet client

- Windows 10 or Windows 11
- A Windows Pointer API-compatible integrated or external pen digitizer
- A working manufacturer pen/touch driver
- Ethernet is recommended for consistent low latency
- The portable build from this branch, or a locally built copy

The tablet's pen must work locally in a Windows Ink test or drawing application before remote testing begins.

### Streaming host

- Apollo, or another host compatible with Moonlight's existing pen/touch protocol
- Native pen and touch input enabled in the host configuration
- A physical or virtual display with an aspect ratio matching the selected stream resolution
- The target drawing applications configured for Windows Ink

No additional inbound port or USB-over-network application is required for this feature.

## Installation

### Option A: use a trusted portable package

No binary is published in this repository. If you have a portable ZIP produced from this source tree:

1. Copy the ZIP to the tablet using trusted local storage or the private LAN.
2. Optionally verify its checksum in PowerShell:

   ```powershell
   Get-FileHash -Algorithm SHA256 .\MoonlightWacomPortable-x64-<version>.zip
   ```

3. Create a new folder dedicated to this fork.
4. Extract the complete ZIP into that folder. Do not run `Moonlight.exe` from inside the ZIP.
5. Confirm that `portable.dat` is beside `Moonlight.exe`. This keeps this portable copy's settings in its own folder.
6. Close any running official Moonlight instance, then start `Moonlight.exe` from the extracted folder.

This does not replace the normally installed Moonlight client. Keep the fork in a separate folder and create a clearly named shortcut if desired. Avoid running the official and experimental clients simultaneously.

Unsigned local builds may trigger Windows reputation warnings. Only run a build you created yourself or received through a trusted channel, and verify its checksum before use.

### Option B: build from source

These instructions assume minimal Windows development experience.

#### 1. Install the development tools

Install:

- Git
- PowerShell 7
- Visual Studio Community 2022
- Visual Studio workload: **Desktop development with C++**
- MSVC v143 x64/x86 build tools
- Windows 10 SDK 19041 or newer
- Qt 6.7 or newer with the **MSVC 2022 64-bit** component
- CMake
- 7-Zip
- Gitleaks
- GitHub CLI only if contributing or pushing a fork
- Windows **Graphics Tools** optional feature for Debug graphics-layer testing

Qt is normally installed with the Qt Online Installer or maintained with the Qt Maintenance Tool. Do not select a MinGW Qt build.

#### 2. Clone the feature branch

Open PowerShell 7 and run:

```powershell
git clone --branch codex/wacom-remote --recurse-submodules https://github.com/inthefleshart/moonlight-qt.git moonlight-wacom
Set-Location .\moonlight-wacom
```

If the repository was cloned without submodules, repair it with:

```powershell
git submodule update --init --recursive
```

#### 3. Check prerequisites

The scripts assume Qt is at `C:\Qt\6.9.2\msvc2022_64` unless `QT_ROOT` is set for the current PowerShell session:

```powershell
$env:QT_ROOT = 'C:\Qt\<qt-version>\msvc2022_64'
pwsh -File .\scripts\bootstrap.ps1 -Check
```

The check prints the missing item and the required Visual Studio or Qt component. To offer Winget installation for missing general command-line tools only:

```powershell
pwsh -File .\scripts\bootstrap.ps1 -InstallMissing
```

The script does not install or invoke Python and does not permanently modify the system `PATH`.

#### 4. Download upstream build dependencies

Run the upstream dependency setup from the repository root:

```powershell
pwsh -File .\setup-deps.ps1
```

Repeat the submodule and dependency setup steps after updating the source revision.

#### 5. Run the Windows-input tests

```powershell
pwsh -File .\scripts\test-windows-input.ps1
```

#### 6. Build and package

Build Release and copy the resulting portable ZIP into the ignored `artifacts` directory:

```powershell
pwsh -File .\scripts\build-windows.ps1 -Configuration Release
pwsh -File .\scripts\package-windows.ps1 -Configuration Release -SkipBuild
```

The packaging command prints the SHA-256 checksum. The portable ZIP is named `MoonlightWacomPortable-x64-<version>.zip`.

For a Debug build:

```powershell
pwsh -File .\scripts\build-windows.ps1 -Configuration Debug
```

The wrapper also supports source paths containing spaces by using a temporary directory junction.

## Host setup

The exact Apollo labels may change between versions, so consult the host's current documentation if the wording differs.

1. Open the Apollo host configuration.
2. Enable its existing native pen and touch input support.
3. Disable USB-over-network forwarding for the integrated digitizer during initial tests.
4. Select one physical or virtual display for initial diagnostics.
5. Start with HEVC, SDR, 2560×1440, 60 FPS.
6. Match the client display and host display aspect ratio.
7. After pen mapping is correct, test 3840×2160 at 60 FPS.
8. Keep the service restricted to the trusted local network. This fork requires no new firewall rule.

## Pairing and first connection

1. Start Apollo on the host.
2. Start this portable Moonlight build on the tablet.
3. Add or select the host using Moonlight's normal pairing process.
4. Enter the displayed pairing PIN on the host when prompted.
5. Open Moonlight settings before launching the first drawing session.
6. Select the host desktop or desired application and begin streaming.

Pairing, video, audio, mouse, keyboard, controller, and virtual-display behavior otherwise follow normal Moonlight/Apollo operation.

## Moonlight configuration

Open **Settings → Input Settings** and review the following fork-specific controls.

### Use native Windows pen input

- Default on for Windows builds.
- Enables pressure, hover, eraser, barrel button, and tilt forwarding from native Windows pointer messages.
- Requires host pen/touch protocol support.
- Restart the active stream after changing it.
- Disable it temporarily when comparing against official Moonlight behavior or diagnosing duplicate input.

### Pen and touch interaction

Start with **Windows default**. Select **Disable touch while pen is in range** only if palm contacts are forwarded while drawing. Use **Always forward touch** when simultaneous pen hover and deliberate touch input are required and the driver handles palm rejection reliably.

### Enable privacy-safe pen diagnostics

When enabled, the current implementation reports aggregate native-pen sample and dropped-event counts at session teardown. It does not record typed keys, application titles, network addresses, file paths, or device serial numbers.

This setting does not yet provide the detailed live diagnostics panel described in the long-term design. Leave it off during normal use.

### Tablet QuickKeys

1. In the tablet manufacturer control panel, assign the first physical key to F13, the second to F14, and continue through F24.
2. In Moonlight, choose a profile.
3. Leave a field empty to pass its F-key through unchanged.
4. Enter one shortcut such as `Ctrl+Z`, `Shift+F5`, or `B` to remap it.
5. Move focus away from the field to save it.
6. Resolve the orange warning if two slots use the same destination shortcut.
7. Use **Reset QuickKey profile** to clear every remap in the selected profile.

Mappings are stored in the portable build's local settings. A profile is selected manually; profiles do not automatically follow the active remote application.

## Creative application setup

Validate the pen in a basic Windows Ink test application first. This separates transport problems from application brush configuration.

### Krita

1. Open Krita's tablet settings.
2. Select its Windows Ink or Windows Pointer input option. The exact label varies by Krita version.
3. Restart Krita if requested.
4. Select a brush preset that visibly responds to pressure.
5. Use Krita's tablet tester before testing a canvas.
6. Check light-to-heavy pressure, hover, eraser, barrel button, tilt, and touch gestures.

### Adobe Photoshop

1. Use Photoshop's current Windows Ink input path.
2. Select a pressure-sensitive brush and enable the desired pressure control in Brush Settings.
3. Test light and heavy strokes before changing driver or application configuration.
4. Verify eraser and tilt using tools that support those properties.
5. Test QuickKey modifier combinations and confirm that modifiers release after focus changes.

Avoid applying obsolete tablet configuration workarounds unless the installed Photoshop version explicitly requires them.

### Substance 3D Painter

1. Select a brush whose size or flow responds to pen pressure.
2. Test short and long strokes on the model.
3. Verify pressure across viewport edges and after rotating the view.
4. Test every mapped QuickKey and modifier combination.
5. Disconnect during a test stroke, reconnect, and confirm no input remains held.

## Recommended validation sequence

Keep additional USB pen tablets unplugged during the first integrated-display test. Their drivers may remain installed.

1. Confirm local tablet pressure and touch without streaming.
2. Confirm normal Moonlight video, keyboard, and mouse behavior.
3. Verify hover without touching the display.
4. Draw a slow pressure ramp from very light to full pressure.
5. Test barrel button and eraser while hovering and drawing.
6. Test horizontal, vertical, and diagonal tilt.
7. Draw into all four corners and the center at 2560×1440.
8. Repeat in windowed and fullscreen modes.
9. Repeat at 3840×2160.
10. Test one- and two-finger gestures with the pen away from the display.
11. Test touch while the pen is hovering under each touch policy.
12. Test every QuickKey in all required profiles.
13. Disconnect during a stroke and while holding a mapped modifier.
14. Complete a sustained drawing session and watch for increasing latency or stuck input.
15. Reconnect any external tablet only after the integrated digitizer passes.

## Troubleshooting

### Video works but pressure does not

- Confirm pressure works locally on the tablet.
- Confirm **Use native Windows pen input** is enabled.
- Restart the stream after changing the setting.
- Confirm native pen/touch input is enabled on the host.
- Confirm the drawing application is using Windows Ink.
- Test with a pressure-sensitive brush and a simple Windows Ink tester.
- Disable USB-over-network forwarding for the integrated digitizer.

### The cursor is offset or cannot reach a corner

- Match host and client aspect ratios.
- Start with a single host display or selected virtual display.
- Match the host display and Moonlight stream resolution.
- Retest after switching between fullscreen and windowed mode.
- Check Windows display rotation and DPI scaling.
- Record only sanitized numeric mapping observations when reporting a bug.

### A stroke is duplicated or also produces mouse clicks

- Confirm native pen input is enabled; it suppresses Moonlight's positively identified SDL pen path.
- Close other remote-access or tablet-forwarding software during the test.
- Unplug additional pen tablets temporarily.
- Compare with native pen input disabled to isolate the source.
- Do not disable ordinary mouse support; real mouse input should remain available.

### Palm touches reach the host

- Confirm the manufacturer driver and Windows palm-rejection settings work locally.
- Select **Disable touch while pen is in range**.
- Retest deliberate touch after moving the pen out of range.

### QuickKeys do nothing

- Confirm each hardware key emits its assigned F13–F24 key locally.
- Ensure the required profile is selected in Moonlight.
- Leave the slot empty and verify the original F-key reaches the host.
- Check that the destination shortcut uses a supported key.
- Resolve duplicate shortcut warnings.
- Remember that application profiles are selected manually.

### Input remains held after a disconnect

- Release the physical pen and all hardware buttons.
- Return focus to the stream window and reconnect.
- Record the exact safe reproduction sequence without including machine or account details.
- If keyboard modifiers remain stuck, press and release each affected modifier once after reconnecting.

## Known limitations

- Physical acceptance testing on the target tablet and host is still pending.
- Krita, Photoshop, and Substance 3D Painter have not yet been certified on physical hardware with this branch.
- The current tests do not prove the two-streamed-pixel corner-accuracy target.
- Detailed live diagnostics, sanitized report export, control highlighting, and detected-device lists are not implemented.
- QuickKey profiles contain user mappings only; no default application shortcut set is supplied.
- QuickKey capture depends on the tablet driver being able to emit F13–F24.
- Automatic application-profile switching is not implemented.
- Wintab Tablet Controls discovery, ownership, rings, strips, and ExpressKey fallback are not implemented.
- `-EnableLocalWintab` currently validates a local SDK path but does not compile a Wintab bridge.
- Wacom SDK headers, sample code, DLLs, and binaries are not redistributed.
- Finger touch continues to use Moonlight's SDL implementation; a native Windows touch-history path has not been added.
- The fork is not code-signed and no public release package is published.

## Privacy and diagnostics

Public reports and commits must not contain:

- Computer or account names
- Private network addresses
- Absolute user-profile paths or UNC paths
- Email addresses, credentials, tokens, or pairing information
- Device serial numbers
- Real logs, packet captures, crash dumps, or screenshots of local configuration
- Proprietary Wacom SDK files or binaries

Before committing changes, run:

```powershell
pwsh -File .\scripts\privacy-check.ps1 -Staged -PrivatePatternFile .\.private\privacy-patterns.txt
gitleaks git --pre-commit --redact --staged
```

Before pushing a feature branch, run the scan over the complete outgoing range:

```powershell
pwsh -File .\scripts\privacy-check.ps1 -Range 'upstream/master..HEAD' -PrivatePatternFile .\.private\privacy-patterns.txt
gitleaks git --redact --log-opts='upstream/master..HEAD' .
```

The repository's local hooks perform these checks when installed. Privacy checks are defense in depth; manually inspect the outgoing file list and diff as well.

## Development notes

The native bridge is intentionally limited to the existing Moonlight/Apollo protocol boundary:

```text
Windows pen digitizer
        ↓ WM_POINTER / GetPointerPenInfoHistory
Moonlight stream-window input thread
        ↓ coordinate, pressure, tool, button, and tilt conversion
Existing Moonlight pen-event protocol
        ↓ encrypted local stream
Apollo host input implementation
        ↓ Windows Ink
Creative application
```

No polling loop runs while idle, and the high-frequency pen path performs no file logging.

Relevant source areas:

- `app/streaming/input/winpointer.*` — Windows message capture and pen state
- `app/streaming/input/inputgeometry.*` — stream-coordinate normalization
- `app/streaming/input/penconversion.*` — tilt conversion
- `app/settings/tabletmappingmanager.*` — QuickKey profiles and mappings
- `app/gui/SettingsView.qml` — fork-specific settings UI
- `tests/windows-input/` — focused conversion and geometry tests

## Updating from upstream

Add and retain the official repository as `upstream`:

```powershell
git remote add upstream https://github.com/moonlight-stream/moonlight-qt.git
git fetch upstream --tags
```

Review the upstream changes and update against an intentional upstream commit. Then reinitialize dependencies, rerun tests, rebuild both configurations, and repeat the privacy checks before pushing. Never push to the upstream remote unless explicitly participating in the upstream contribution process.

## Upstream Moonlight features

This fork retains Moonlight PC's normal streaming capabilities, including hardware-accelerated decoding, H.264/HEVC/AV1 support where available, HDR, surround audio, multitouch, controller support, relative and absolute pointer modes, and system-shortcut forwarding.

For general Moonlight documentation and platform downloads, visit the [official Moonlight website](https://moonlight-stream.org). Official Moonlight releases do not contain this fork's experimental native Windows pen changes.

## Contributing

Keep changes focused, Windows code behind the existing platform guards, and public material fully sanitized. Build and test both affected configurations before requesting review. Do not add Wacom SDK material until its redistribution terms and GPL compatibility have been reviewed explicitly.

## License

This fork remains subject to the upstream Moonlight PC license. See [LICENSE](LICENSE) and the notices already included in the repository. Third-party names and trademarks belong to their respective owners.
