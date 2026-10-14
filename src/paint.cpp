// GDI+ relies on the Windows min/max macros, so it stays confined to this file.
#include "paint.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>

namespace {

ULONG_PTR token;

uint32_t swapRB(uint32_t p) { return (p & 0xFF00FF00) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16); }

std::wstring widen(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

}  // namespace

namespace paint {

void startup() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
}

void shutdown() { Gdiplus::GdiplusShutdown(token); }

// Rasterises printable ASCII with GDI, deliberately without antialiasing: the
// hard pixel edges are what make it read as a console-era font once upscaled.
FontAtlas font(const wchar_t* face, int px, bool bold) {
    FontAtlas atlas;
    const int W = 512, H = 512;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HFONT f = CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          NONANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ oldFont = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    atlas.lineHeight = tm.tmHeight;

    int x = 1, y = 1;
    for (int c = 32; c < 128; c++) {
        ABC abc{};
        wchar_t ch = c == 127 ? wchar_t(0x00B7) : wchar_t(c);  // slot 127 holds the middle dot
        if (!GetCharABCWidthsW(dc, ch, ch, &abc)) {
            SIZE s;
            GetTextExtentPoint32W(dc, &ch, 1, &s);
            abc = {0, UINT(s.cx), 0};
        }
        int boxW = int(abc.abcB) + 2;
        if (x + boxW + 1 >= W) x = 1, y += tm.tmHeight + 2;
        TextOutW(dc, x - abc.abcA + 1, y, &ch, 1);
        Glyph& g = atlas.glyphs[c - 32];
        g = {x, y, boxW, int(tm.tmHeight), abc.abcA - 1, abc.abcA + int(abc.abcB) + abc.abcC};
        x += boxW + 2;
    }
    GdiFlush();

    atlas.image.w = W;
    atlas.image.h = H;
    atlas.image.rgba.resize(size_t(W) * H);
    const uint32_t* src = static_cast<const uint32_t*>(bits);
    for (size_t i = 0; i < atlas.image.rgba.size(); i++) atlas.image.rgba[i] = (src[i] & 0xFF) > 127 ? 0xFFFFFFFF : 0x00FFFFFF;

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(f);
    DeleteObject(bmp);
    DeleteDC(dc);
    return atlas;
}

bool savePng(const Pixels& img, const char* path) {
    Gdiplus::Bitmap bmp(img.w, img.h, PixelFormat32bppARGB);
    Gdiplus::BitmapData data;
    Gdiplus::Rect r(0, 0, img.w, img.h);
    bmp.LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data);
    for (int y = 0; y < img.h; y++) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<BYTE*>(data.Scan0) + y * data.Stride);
        for (int x = 0; x < img.w; x++) row[x] = swapRB(img.rgba[size_t(y) * img.w + x]) | 0xFF000000;
    }
    bmp.UnlockBits(&data);
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return bmp.Save(widen(path).c_str(), &png, nullptr) == Gdiplus::Ok;
}

}  // namespace paint
