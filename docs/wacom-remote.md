# Native Windows Pen, Touch, and Artist QuickKeys

This Windows-only Moonlight fork captures integrated pen-display input with `WM_POINTER` and forwards it through Moonlight's existing Apollo/Sunshine pen, touch, keyboard, and mouse channels. It installs no driver, service, virtual device, firewall rule, or additional network protocol.

## Client settings

Open **Settings > Input Settings**:

- **Use native Windows pen input**: keep enabled for a Windows Pointer API pen display.
- **Pen cursor visibility**: use **Automatic** initially. It hides only Moonlight's local system cursor while the pen is in range; the remote application's brush cursor remains visible.
- **Pen and touch interaction**: begin with **Windows default**. Use pen-priority only if unwanted palm contacts reach the host.
- **Privacy-safe pen diagnostics**: enable temporarily to collect aggregate batch, retained-sample, truncation, and processing-time counters. Typed keys and machine identifiers are never recorded.

Restart an active stream after changing native-pen settings.

## QuickKey setup

The first 12 logical sources remain F13 through F24 for compatibility. QuickKeys 13–18 begin without a source:

1. Configure each physical tablet control to emit a unique keyboard key or chord.
2. Select a shipped profile in Moonlight.
3. Press **Source: Learn source** on the desired row.
4. Press the physical tablet control.
5. Choose **Record shortcut**, **Sequence**, **Pen gesture**, or **Common actions**.

The shortcut recorder requires no typed syntax. Escape cancels and Backspace clears. Enable **Hold until the QuickKey is released** for temporary modifiers or tools. The sequence recorder supports up to 32 ordered shortcut steps.

The pen-gesture editor combines optional Ctrl, Alt, Shift, Win, B, F, M, or Space input with left, middle, right, X1, or X2 mouse and absolute pen movement. This supports Alt+Middle Mouse+Pen Drag, B+Middle Mouse+Pen Drag, and similar viewport or brush controls. Mouse buttons are released before modifiers on contact end, QuickKey release, focus loss, capture loss, profile change, disconnect, or shutdown.

Preset switching updates all 18 rows immediately. Customized rows display **Custom** and are not overwritten by later preset changes. **Reset** restores one shipped row; **Reset QuickKey profile** restores all 18.

## Shipped profiles

Application presets:

- Krita
- Photoshop
- Substance 3D Painter
- ZBrush — Right-Click Navigation
- Mudbox
- Mari
- Daz Studio — Keyboard Navigation
- 3DCoat — Default Navigation
- Maya
- 3ds Max — Standard Interaction
- Blender — Default Keymap
- Marmoset Toolbag

Generic presets:

- Blank / Pass-Through
- Windows 10 Remote
- Windows 11 Remote
- Maya-Style 3D Navigation
- 3ds Max-Style 3D Navigation
- Generic Sculpting
- Generic Texture Painting

Application presets reserve slot 16 for touch forwarding, slot 17 for Wacom Radial Menu setup, and slot 18 for a custom action. Those slots still require learned physical sources.

### Navigation requirements

- **ZBrush**: enable **Preferences > Interface > RightClick Navigation**. The preset uses Right Mouse to rotate, Alt+Right Mouse to pan, and Ctrl+Right Mouse to scale.
- **3DCoat**: use its default navigation scheme or re-record orbit, pan, and zoom.
- **3ds Max**: use standard 3ds Max interaction mode. Maya interaction mode uses different gestures.
- **Blender**: use Blender Default. Industry Compatible needs customized navigation rows.
- **Daz Studio**: the preset uses keyboard viewport navigation because mouse navigation is configurable.

Creative applications can change shortcuts across versions. Treat presets as safe starting points and record overrides for custom keymaps.

## Common actions

The chooser includes:

- Touch forwarding on/off
- Touch-policy cycling
- Diagnostics toggle
- Reset stuck input
- Middle- and right-click pen gestures
- File Explorer (`Win+E`)
- Windows 10 Action Center (`Win+A`)
- Windows 11 Quick Settings (`Win+A`)
- Windows 11 Notifications (`Win+N`)
- On-Screen Keyboard (`Win+Ctrl+O`)
- Show desktop and forward/reverse task switching
- Wacom Radial Menu chord

Touch forwarding affects only finger input sent to the host; it does not disable the tablet's local Windows touch hardware. The keyboard action is accurately labelled **On-Screen Keyboard**, because the modern touch keyboard has no equally reliable global shortcut.

Open/Run Application, arbitrary typed text, executable paths, scripts, and shell commands are deliberately unavailable.

### Wacom Radial Menu

Configure a unique chord for Radial Menu in Wacom Center or the host's Wacom control panel. Record that chord into slot 17. The row remains **setup required** until configured. Moonlight forwards the chord normally and does not call proprietary Wacom APIs or bundle Wacom SDK content.

## Apollo host setup

Enable native pen and touch input. Start with one virtual display at 2560×1440, 60 Hz, HEVC, and SDR. Match the virtual-display and client aspect ratios. After mapping and pressure pass, test 3840×2160 at 60 Hz. Disable USB-over-network forwarding for the integrated digitizer.

Application settings:

- Krita: select Windows Ink tablet input.
- Photoshop: use its current Windows Ink path.
- Substance 3D Painter: enable a pressure-sensitive brush parameter.
- Other shipped applications: retain their documented default Windows keymap unless the preset name says otherwise.

## Host diagnostic canvas

Packaging produces a separate portable `MoonlightPenDiagnostics.exe`:

1. Start it on the Windows host.
2. Connect from the tablet through Moonlight.
3. Draw inside the diagnostic canvas.
4. Inspect pressure, tilt, hover/contact phase, barrel, eraser, event rate, and active touch count.
5. Press **Clear / Reset** between tests.

The utility stores only the current in-memory graph and stroke. It writes no log, registers no global hotkey, launches no process, installs no service, and creates no startup entry.

Build it separately with:

```powershell
pwsh -File .\scripts\build-pen-diagnostics.ps1 -Configuration Release
```

## Validation checklist

1. Unplug an external tablet for initial integrated-display testing.
2. Verify hover, light-to-full pressure, barrel, eraser, and tilt.
3. Draw fast circles and long diagonals; confirm there is no skip-and-catch-up behavior.
4. Confirm the local default cursor no longer flickers over the application's brush cursor.
5. Verify one-, two-, and multi-finger touch while the pen is away.
6. Draw into all corners at 1440p and 4K, then repeat after resizing and fullscreen changes.
7. Hold and release every navigation gesture during contact.
8. Disconnect during a stroke and confirm no mouse button or modifier remains held.
9. Learn all 18 physical controls and switch repeatedly between shipped presets.
10. Run a 30-minute mixed pen/touch/navigation session and watch for increasing latency or memory use.

## Build and privacy

Run:

```powershell
pwsh -File .\scripts\bootstrap.ps1 -Check
pwsh -File .\scripts\test-windows-input.ps1
pwsh -File .\scripts\build-windows.ps1 -Configuration Release
pwsh -File .\scripts\package-windows.ps1 -Configuration Release -SkipBuild
```

No Python is used. Do not publish real logs, captures, screenshots, device serials, network addresses, computer names, user-profile paths, or local settings. Before pushing, run:

```powershell
pwsh -File .\scripts\privacy-check.ps1 -Range upstream/master..HEAD
gitleaks git --redact --log-opts="upstream/master..HEAD" .
```
