# Changelog

## 0.20.8

- Renders battery numbers with Windows-hinted Segoe UI Semibold at the actual pixel size of the tray icon.
- Uses grayscale antialiasing and a transparent background. Three-digit values use a condensed native font so 100 remains readable.

## 0.20.7

- Redraws the tray microphone and battery at the taskbar’s current DPI. Battery number mode uses the full icon space for the digits and shows the complete percentage on hover.
- Adds English and Italian language settings for the desktop panel, audio wizard, tray, overlay and NovaMix’s base display.
- Updates the bundled Electron runtime to 45.2.3 and verifies the upstream archive’s SHA-256 at build time.
- Packages the native dual-stream mixer, desktop panel, guided Windows installer and English setup instructions for Windows x64.
- Preserves the routes and preferences of existing NovaMix 0.20 installations.
