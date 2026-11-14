# Changelog

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
