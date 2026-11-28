# Changelog

## 0.21.0-alpha.3 — keyboard volume follows the selected output

- Suspends keyboard volume interception when the Windows default output is outside the Nova route, so Bluetooth speakers, HDMI and other outputs retain normal Windows volume/mute controls.
- Automatically restores physical headset volume keys when returning to NovaMix PC/Chat routed to the Nova headset, or directly to the Nova headset.
- Uses endpoint IDs and the adapter property, so renamed stream outputs still work. If NovaMix is configured to play through another device, virtual-stream defaults do not capture headset volume keys.
- Reacts to default-output/device notifications, releases the capture gate immediately and rejects stale keyboard commands queued before a route switch.
- Adds keyboard routing diagnostics and regression tests for route switching, notification roles, mute, disabled/stopped states and queued command invalidation.

## 0.21.0-alpha.2 — scrolling and window layout

- Uses Direct3D 12 accelerated Iced rendering, retaining a software fallback.
- Optimizes the Rust release build for execution speed and removes scroll-time pixel snapping.
- Animates wheel scrolling inside the widget without an idle timer or per-frame application view rebuild; touchpad pixels and scrollbar dragging stay direct.
- Disables title-bar maximization while preserving resizing, minimize and close.
- Centers the page in wide windows and resets scrolling when switching tabs.
- Adds UI update timing checks using synthetic Settings data and regression checks for smooth scrolling bounds, reversal and settling.

## 0.21.0-alpha.1 — native UI preview

- Replaces the Electron panel with a native Rust/Iced interface using Tiny Skia.
- Packages no Chromium, Node.js or WebView2 runtime in the native installer.
- Keeps the C++ audio/HID engine for the first migration stage, including the headset wheel, OLED, overlay and native tray indicators.
- Recreates the mixer, routes, hardware status and English/Italian settings in native widgets.
- Uses bounded background IPC, coalesced slider targets and revision checks for responsive controls.
- Preserves installed routing/preferences, and replaces the Electron frontend when updating.
- Includes native diagnostic export and original Rust dependency license texts.
- Adds synthetic preview integration/lifecycle checks and reproducible UI resource measurements.

## 0.20.12

- Centers tray numbers by their visible margins, including asymmetric pairs such as 12.
- Aligns the background to the pixel grid so it has equal margins around the icon.
- Adds a rendering regression check for the reported white-on-pink 12 example.

## 0.20.11

- Separates the Language and Accent color cards with consistent spacing.
- Adds an independent background size setting from 8 to 16 pixels. The default square uses the full tray icon area.
- Centers the visible number while retaining its complete antialiased edges.

## 0.20.10

- Uses smaller regular Segoe UI digits with an adjustable size from 8 to 16 pixels.
- Adds a battery number background toggle. With a background, the three battery colors apply to the tile and the number is white. Without a background, the colors apply to the number.
- Shows Update in the installer when NovaMix is already installed.

## 0.20.9

- Adds a rounded dark background behind the tray battery number, matching the microphone icon.
- Keeps the native Segoe UI Semibold font and smooth text edges.

## 0.20.8

- Renders battery numbers with Windows-hinted Segoe UI Semibold at the actual pixel size of the tray icon.
- Uses grayscale antialiasing and a transparent background. Three-digit values use a condensed native font so 100 remains readable.

## 0.20.7

- Redraws the tray microphone and battery at the taskbar’s current DPI. Battery number mode uses the full icon space for the digits and shows the complete percentage on hover.
- Adds English and Italian language settings for the desktop panel, audio wizard, tray, overlay and NovaMix’s base display.
- Updates the bundled Electron runtime to 45.2.3 and verifies the upstream archive’s SHA-256 at build time.
- Packages the native dual-stream mixer, desktop panel, guided Windows installer and English setup instructions for Windows x64.
- Preserves the routes and preferences of existing NovaMix 0.20 installations.
