#pragma once
#include <cmath>
#include <gdiplus.h>

namespace trayStatus {
constexpr UINT callback = WM_APP + 21;
constexpr int unknown = -1;
constexpr int glyphSize = 16;
constexpr uint32_t outline = 0xff222936, white = 0xfff6f8fc;
constexpr uint32_t mint = 0xff79e4b9, muted = 0xffff8f91, gray = 0xffbdc7d6;
constexpr uint32_t rgbMask = 0xffffff, percentageStyle = 0x1000000;
constexpr uint32_t backgroundStyle = 0x2000000;
constexpr int fontSizeShift = 26;
constexpr int backgroundSizeShift = 30;
constexpr int defaultThreshold = 25;


struct BatterySettings {
    bool showPercent = false;
    bool background = true;
    int fontSize = 12;
    int backgroundSize = 16;
    bool notify = false;
    int threshold = defaultThreshold;
    int lowLimit = 25, mediumLimit = 50;
    int low = 0xff8f91, medium = 0xffc36d, normal = 0x79e4b9;

    static bool validLimits(int low, int medium) {
        return low >= 0 && low < medium && medium < 100;
    }

    uint64_t style(int percent) const {
        if (percent < 0) return gray & rgbMask;
        int color = percent <= lowLimit ? low : percent <= mediumLimit ? medium : normal;
        const uint64_t numberStyle = percentageStyle | (uint64_t(fontSize - 8) << fontSizeShift) |
            (background ? backgroundStyle | (uint64_t(backgroundSize - 8) << backgroundSizeShift) : 0);
        return uint32_t(color) | (showPercent ? numberStyle : 0);
    }
};

inline void roundedPath(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
    const float d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

inline void tile(Gdiplus::Graphics& g, float x, float y, float w, float h, float r, uint32_t color) {
    Gdiplus::GraphicsPath path;
    Gdiplus::SolidBrush brush{Gdiplus::Color(color)};
    roundedPath(path, x, y, w, h, r);
    g.FillPath(&brush, &path);
}

inline void microphone(Gdiplus::Graphics& g, int state) {
    tile(g, 0.5f, 0.5f, 15, 15, 4, outline);
    const uint32_t color = state < 0 ? gray : state == 0 ? white : muted;
    tile(g, 6, 2.5f, 4, 7.3f, 2, color);
    Gdiplus::Pen pen(Gdiplus::Color(color), 1.25f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawArc(&pen, 4.4f, 4.3f, 7.2f, 7.2f, 0, 180);
    g.DrawLine(&pen, 8.0f, 11.5f, 8.0f, 13.5f);
    g.DrawLine(&pen, 6.5f, 13.5f, 9.5f, 13.5f);
    if (state == 1) {
        Gdiplus::Pen gap(Gdiplus::Color(outline), 3);
        g.DrawLine(&gap, 3.2f, 3.0f, 13.0f, 12.8f);
        g.DrawLine(&pen, 3.2f, 3.0f, 13.0f, 12.8f);
    }
}

inline bool percentage(Gdiplus::Bitmap& bitmap, int percent, uint32_t color, int size, int fontSize) {
    Gdiplus::Bitmap text(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&text);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    }
    // GDI hints the font at its final pixel size. Scaling glyph outlines afterwards
    // loses those adjustments and makes small tray numbers uneven or blurry.
    const int canvas = size * 3;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = canvas;
    info.bmiHeader.biHeight = -canvas;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* data = nullptr;
    HBITMAP mask = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!mask || !dc) {
        if (mask) DeleteObject(mask);
        if (dc) DeleteDC(dc);
        return false;
    }
    const auto oldBitmap = SelectObject(dc, mask);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, OPAQUE);
    const auto label = std::to_wstring(percent);
    auto* coverage = static_cast<uint32_t*>(data);
    RECT ink{};
    bool drawn = false;
    for (int height = MulDiv(fontSize, size, glyphSize); height >= size / 2; --height) {
        const int fontWidth = label.size() == 3 ? std::max(1, height / 3) : 0;
        HFONT font = CreateFontW(-height, fontWidth, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) break;
        const auto oldFont = SelectObject(dc, font);
        std::fill_n(coverage, canvas * canvas, 0u);
        const BOOL rendered = TextOutW(dc, 0, 0, label.c_str(), int(label.size()));
        GdiFlush();
        SelectObject(dc, oldFont);
        DeleteObject(font);
        if (!rendered) break;
        // Use coverage-weighted positioning for optical centering. Keep the
        // visible bounds inside the icon, including asymmetric digits such as 1.
        ink = {canvas, canvas, 0, 0};
        uint64_t totalCoverage = 0, weightedX = 0, weightedY = 0;
        for (int y = 0; y < canvas; ++y) {
            for (int x = 0; x < canvas; ++x) {
                const auto alpha = coverage[y * canvas + x] & 255;
                totalCoverage += alpha;
                weightedX += uint64_t(2 * x + 1) * alpha;
                weightedY += uint64_t(2 * y + 1) * alpha;
                if (alpha < 64) continue;
                ink.left = std::min(ink.left, LONG(x));
                ink.top = std::min(ink.top, LONG(y));
                ink.right = std::max(ink.right, LONG(x + 1));
                ink.bottom = std::max(ink.bottom, LONG(y + 1));
            }
        }
        const int width = ink.right - ink.left, inkHeight = ink.bottom - ink.top;
        if (width <= 0 || inkHeight <= 0 || width > size || inkHeight > size - 2) continue;
        const double centerX = double(weightedX) / (2 * totalCoverage);
        const double centerY = double(weightedY) / (2 * totalCoverage);
        const int left = std::clamp(int(std::lround(size / 2.0 - centerX)) + int(ink.left), 0, size - width);
        const int top = std::clamp(int(std::lround(size / 2.0 - centerY)) + int(ink.top), 0, size - inkHeight);
        // Grayscale coverage keeps the small glyph edges smooth without
        // ClearType's colored fringes. Composite the text over the tray tile.
        for (int y = 0; y < canvas; ++y) {
            for (int x = 0; x < canvas; ++x) {
                const BYTE alpha = BYTE(coverage[y * canvas + x] & 255);
                const int targetX = left + x - ink.left, targetY = top + y - ink.top;
                if (!alpha || targetX < 0 || targetX >= size || targetY < 0 || targetY >= size) continue;
                text.SetPixel(targetX, targetY, Gdiplus::Color(alpha, (color >> 16) & 255,
                                                           (color >> 8) & 255, color & 255));
            }
        }
        drawn = true;
        break;
    }
    SelectObject(dc, oldBitmap);
    DeleteDC(dc);
    DeleteObject(mask);
    if (drawn) {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.DrawImage(&text, 0, 0);
    }
    return drawn;
}

inline void battery(Gdiplus::Graphics& g, int percent, uint64_t style) {
    percent = std::min(percent, 100);
    const uint32_t color = 0xff000000 | uint32_t(style & rgbMask);
    tile(g, 0.3f, 3, 13.8f, 10, 2.4f, outline);
    tile(g, 13, 5.5f, 2.7f, 5, 1.2f, outline);
    tile(g, 13.4f, 6.5f, 1.3f, 3, 0.5f, white);
    tile(g, 1.3f, 4, 11.8f, 8, 1.7f, white);
    tile(g, 2.5f, 5.2f, 9.4f, 5.6f, 1, outline);
    if (percent > 0) {
        const float width = std::max(0.8f, 9.4f * percent / 100);
        tile(g, 2.5f, 5.2f, width, 5.6f, std::min(1.0f, width / 2), color);
    } else if (percent < 0) {
        tile(g, 5.1f, 7.2f, 4.2f, 1.6f, 0.8f, gray);
    }
}

inline HICON createIcon(bool mic, int value, int size, uint64_t style = 0) {
    struct Runtime {
        ULONG_PTR token = 0;
        Runtime() { Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token, &input, nullptr); }
        ~Runtime() { if (token) Gdiplus::GdiplusShutdown(token); }
    };
    static const Runtime runtime;
    if (!runtime.token) return nullptr;
    Gdiplus::Bitmap bitmap(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        const float scale = float(size) / glyphSize;
        graphics.ScaleTransform(scale, scale);
        if (mic) microphone(graphics, value);
        else if (value < 0 || !(style & percentageStyle)) battery(graphics, value, style);
        else if (style & backgroundStyle) {
            const float side = float((style >> backgroundSizeShift) & 15) + 8;
            const float inset = (glyphSize - side) / 2;
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            tile(graphics, inset, inset, side, side, 1, 0xff000000 | uint32_t(style & rgbMask));
        }
    }
    if (!mic && value >= 0 && (style & percentageStyle)) {
        const uint32_t color = (style & backgroundStyle) ? 0xffffff : uint32_t(style & rgbMask);
        const int fontSize = int((style >> fontSizeShift) & 15) + 8;
        if (!percentage(bitmap, std::min(value, 100), color, size, fontSize)) return nullptr;
    }
    HICON icon = nullptr;
    return bitmap.GetHICON(&icon) == Gdiplus::Ok ? icon : nullptr;
}

inline int iconSize() {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    const auto context = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const UINT dpi = taskbar ? GetDpiForWindow(taskbar) : GetDpiForSystem();
    if (context) SetThreadDpiAwarenessContext(context);
    const int size = dpi ? GetSystemMetricsForDpi(SM_CXSMICON, dpi) : 0;
    return size > 0 ? size : glyphSize;
}
class Icon {
    UINT id;
    GUID guid;
    bool registered = false;
    int lastValue = unknown, lastSize = 0;
    uint64_t lastStyle = 0;
    bool lastEnglish = false;
    ULONGLONG retryAt = 0;

    NOTIFYICONDATAW identity(HWND owner, bool temporary) const {
        NOTIFYICONDATAW item{};
        item.cbSize = sizeof(item);
        item.hWnd = owner;
        item.uID = id;
        if (!temporary) {
            item.uFlags = NIF_GUID;
            item.guidItem = guid;
        }
        return item;
    }

public:
    Icon(UINT id, GUID guid) : id(id), guid(guid) {}

    void update(HWND owner, bool temporary, int value, int size, bool force, bool english, uint64_t style = 0) {
        if (!force && !retryAt && registered && value == lastValue && size == lastSize && style == lastStyle && english == lastEnglish) return;
        auto now = GetTickCount64();
        if (!force && now < retryAt) return;
        auto item = identity(owner, temporary);
        item.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        item.uCallbackMessage = callback;
        const bool mic = id == 2;
        item.hIcon = createIcon(mic, value, size, style);
        std::wstring tip;
        if (mic) tip = value < 0 ? L"NovaMix · Microfono non disponibile"
                                : value == 0 ? L"NovaMix · Microfono attivo" : L"NovaMix · Microfono muto";
        else tip = value < 0 ? L"NovaMix · Batteria cuffie non disponibile"
                             : L"NovaMix · Batteria cuffie " + std::to_wstring(value) + L"%";
        if (english) {
            if (mic) tip = value < 0 ? L"NovaMix · Microphone unavailable"
                                    : value == 0 ? L"NovaMix · Microphone on" : L"NovaMix · Microphone muted";
            else tip = value < 0 ? L"NovaMix · Headset battery unavailable"
                                 : L"NovaMix · Headset battery " + std::to_wstring(value) + L"%";
        }
        wcscpy_s(item.szTip, tip.c_str());
        if (!item.hIcon) {
            retryAt = now + 1000;
            return;
        }
        bool modified = registered && Shell_NotifyIconW(NIM_MODIFY, &item);
        bool added = !modified && Shell_NotifyIconW(NIM_ADD, &item);
        DestroyIcon(item.hIcon);
        registered = modified || added;
        retryAt = registered ? 0 : now + 1000;
        if (!registered) return;
        lastValue = value;
        lastSize = size;
        lastStyle = style;
        lastEnglish = english;
        if (added) {
            auto version = identity(owner, temporary);
            version.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &version);
        }
    }

    bool warnLowBattery(HWND owner, int percent, bool english) const {
        if (!registered) return false;
        auto item = identity(owner, false);
        item.uFlags |= NIF_INFO;
        item.dwInfoFlags = NIIF_WARNING;
        wcscpy_s(item.szInfoTitle, english ? L"NovaMix · Headset battery" : L"NovaMix · Batteria cuffie");
        swprintf_s(item.szInfo, english ? L"Arctis battery is at %d%%. Time to recharge." : L"Le Arctis sono al %d%%. È il momento di ricaricarle.", percent);
        return Shell_NotifyIconW(NIM_MODIFY, &item) != FALSE;
    }

    void remove(HWND owner, bool temporary) const {
        auto item = identity(owner, temporary);
        Shell_NotifyIconW(NIM_DELETE, &item);
    }
};

inline Icon micIcon(2, {0xeb471c09, 0x90f4, 0x4576, {0xa2, 0x64, 0xce, 0x75, 0xd4, 0x6f, 0xf2, 0x8e}});
inline Icon batteryIcon(3, {0x69db03c1, 0x4aa1, 0x41d6, {0x94, 0x3e, 0xe5, 0x71, 0x12, 0xb6, 0x85, 0xc8}});

struct BatteryAlert {
    int threshold = 0;
    bool notified = false;
    ULONGLONG retryAt = 0;

    bool due(int percent, int newThreshold, ULONGLONG now) {
        if (threshold != newThreshold) {
            threshold = newThreshold;
            notified = false;
            retryAt = 0;
        }
        if (!threshold || percent < 0) return false;
        if (percent >= std::min(100, threshold + 5)) {
            notified = false;
            retryAt = 0;
        }
        return percent <= threshold && !notified && now >= retryAt;
    }

    void sent(bool success, ULONGLONG now) {
        notified = success;
        retryAt = success ? 0 : now + 5000;
    }
};

inline void update(HWND owner, bool temporary, int mic, int percent, const BatterySettings& settings,
                   bool english, bool force = false, bool refreshScale = false) {
    static int size = glyphSize;
    if (force || refreshScale) size = iconSize();
    micIcon.update(owner, temporary, mic, size, force, english);
    batteryIcon.update(owner, temporary, percent, size, force, english, settings.style(percent));
    static BatteryAlert alert;
    if (!temporary) {
        const auto now = GetTickCount64();
        if (alert.due(percent, settings.notify ? settings.threshold : 0, now))
            alert.sent(batteryIcon.warnLowBattery(owner, percent, english), now);
    }
}

inline void remove(HWND owner, bool temporary) {
    micIcon.remove(owner, temporary);
    batteryIcon.remove(owner, temporary);
}
}
