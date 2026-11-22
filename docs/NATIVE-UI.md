# Native Iced interface preview

Version: **0.21.0-alpha.2**, Windows 11 x64.

This is the first stage of NovaMix's Rust migration: a real native desktop panel connected to the existing C++ engine. It is not a complete Rust rewrite of the audio/HID engine. The native installer does not contain Electron, Chromium, Node.js, a JavaScript frontend or WebView2. Iced uses wgpu/Direct3D 12 acceleration and falls back to Tiny Skia if GPU initialization is unavailable. Release builds optimize for execution speed.

Wheel notches animate inside the scroll widget without rebuilding the entire application view for every animation frame. Pixel/touchpad scrolling and dragging the scrollbar remain direct. Animation stops when settled and does not use an idle animation timer. Text can move at subpixel positions during scrolling. The native window retains resizing, minimize and close, disables title-bar maximization, and centers the page within wide windows.

## Included controls

- ChatMix slider, center and 1% fine adjustment.
- Physical headset volume and combined software mix volume.
- Live hardware microphone mute, headset battery and charger-slot battery.
- PC/Chat/playback endpoint selection and input signal meters. Active routes are protected until playback is stopped.
- Italian/English and three accent colors.
- Tray percentage/background, independent font and background sizes, three editable hex battery colors, both battery color thresholds and a notification threshold.
- Keyboard volume, ChatMix curve, overlay, base display and microphone alert settings.
- Native Windows Save dialog for diagnostic exports.
- Close-to-tray behavior: closing the window exits the Iced panel, while the engine keeps running. Open NovaMix from the tray to launch the panel again. Quit stops the engine.

The headset wheel, OLED drawing, actual audio processing and tray icons remain owned by the existing engine. They continue working while the panel is closed. VB-CABLE and Windows application routing work as in 0.20.12; no driver reinstall is required for an existing working configuration.

## Updating and returning to stable

Quit NovaMix from its tray menu before running the preview installer. It updates the existing installation in place, preserves `streams.ini`, keeps the existing tray identities and replaces the old Electron frontend directory. Settings are not copied from a developer's PC into the package.

To return to 0.20.12, quit the preview and run the stable installer. It restores the Electron frontend while retaining the same routing preferences.

## Build

Install the Rust MSVC toolchain (1.88 or newer), Visual Studio C++ Build Tools and Windows 11 SDK. Cargo must be on PATH.

```powershell
# Run the meaningful transport/queue/input tests and build the panel.
.\build-native-ui.ps1 -RunTests

# Build the complete guided installer. The vendor driver remains unmodified.
.\build-release.ps1 -Frontend Iced

# Extract the installer into a NEW, isolated folder for verification.
# Run this from PowerShell; wait for the process to finish.
Start-Process .\dist\NovaMix-Setup-x64.exe -ArgumentList '--verify-package', 'C:\NovaMix-QA' -Wait
.\native-ui\verify-preview.ps1 -AppDirectory C:\NovaMix-QA\app
```

`-CargoPath` selects a specific Cargo executable. `-CableArchive` selects a checksum-verified copy of the original VB-CABLE Pack45 ZIP. Cargo.lock pins dependencies, including checksums; the Windows runtime license texts are packaged under `frontend/licenses`.

For a panel-only visual demo, run `.\.build\iced-frontend\NovaMixUi.exe --demo`. Demo mode does not connect to the engine. `--demo --capture-dir <folder>` saves synthetic previews and exits automatically. It cannot capture a connected user's headset/device state.

## Verification and performance

`verify-preview.ps1` runs the engine's core, API, launcher and stream checks, then uses the Rust client against `--preview` synthetic state to verify controls and route protection. It checks production panel launch and exit after the parent process ends. It does not start real audio or HID workers or register real tray icons.

The script records UI executable size, working set, private memory and CPU time in `test-iced-resources.json`. These are measurements for the synthetic preview on the test machine, not guaranteed live audio resource usage. Closing the panel releases its memory; the engine remains running.

In the initial **alpha.1 software-rendered** Windows preview check, the complete installer was 3,727,872 bytes (about 3.7 MB), the UI executable was about 3.4 MB, and the panel used approximately 25–28 MiB working set. These are historical alpha.1 figures; GPU rendering in alpha.2 changes the binary size and memory footprint. The figures exclude the audio engine. The previous 0.20.12 installer was 164,077,568 bytes.

In the **alpha.2 GPU** preview check, the installer was 5,744,128 bytes (about 5.7 MB), and the open panel used approximately 152 MiB working set. The GPU backend increases memory use while the panel is open. Closing the panel releases its process and graphics resources while the mixer continues running.

To compare rendering with synthetic data, run `NovaMixUi.exe --demo --scroll-benchmark <output.json>`. The check scrolls the Settings page on a 16 ms update schedule, records UI update intervals and scheduling delay after warm-up, verifies the maximize style, and exits. These are UI responsiveness measurements, not GPU presentation FPS. Set the process-local environment variable `ICED_BACKEND` to `wgpu` or `tiny-skia` to compare GPU/software rendering. This test does not connect to the engine or access hardware and does not measure audible audio latency.

In one comparison on the test PC, the 95th-percentile UI update delay was approximately 124 ms with software rendering and 2.2 ms with Direct3D 12. The corresponding 95th-percentile update intervals were approximately 64 ms and 17 ms. Results depend on hardware, graphics drivers, display scaling and other activity.

The Rust client uses bounded loopback-only HTTP requests on a worker thread. The panel receives state-change events and has no idle UI polling timer. It does not follow redirects or accept arbitrary remote addresses. Slider targets are coalesced without moving them ahead of start/stop/route commands. Revision checks prevent an older poll response from replacing a newer slider value. Process-local session tokens are excluded from logs and screenshots.

## Next migration stage

Port the existing DSP, WASAPI and HID/OLED implementation to Rust module by module, with comparison against the C++ reference. The native panel can remain unchanged during that work. Hardware feedback and audible behavior still require real-headset verification in addition to automated preview checks.
