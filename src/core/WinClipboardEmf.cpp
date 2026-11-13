/**
 * @file WinClipboardEmf.cpp
 * @brief 見 WinClipboardEmf.h 檔頭說明。
 */
#include "WinClipboardEmf.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace aicad {
namespace core {

#ifdef _WIN32

namespace {

std::string formatLabel(UINT fmt)
{
    switch (fmt) {
    case CF_TEXT:          return "Text";
    case CF_BITMAP:        return "Bitmap";
    case CF_METAFILEPICT:  return "Metafile";
    case CF_DIB:           return "DIB";
    case CF_UNICODETEXT:   return "UnicodeText";
    case CF_ENHMETAFILE:   return "Enhanced Metafile";
    case CF_HDROP:         return "FileDrop";
    case CF_DIBV5:         return "DIBv5";
    default: break;
    }
    wchar_t wbuf[128] = {0};
    const int n = GetClipboardFormatNameW(fmt, wbuf, 127);   // 自訂格式（例如 AutoCAD.r18）
    if (n > 0) {
        char buf[256] = {0};
        const int m = WideCharToMultiByte(CP_UTF8, 0, wbuf, n, buf, 255, nullptr, nullptr);
        if (m > 0) return std::string(buf, size_t(m));
    }
    return "format#" + std::to_string(unsigned(fmt));
}

} // namespace

bool readClipboardEmf(std::vector<unsigned char>& out, std::string& formats)
{
    out.clear();
    formats.clear();

    // 剪貼簿可能正被別的程式占用，短暫重試
    bool opened = false;
    for (int i = 0; i < 10 && !opened; ++i) {
        opened = OpenClipboard(nullptr) != 0;
        if (!opened) Sleep(30);
    }
    if (!opened) {
        formats = "(clipboard is busy)";
        return false;
    }

    for (UINT f = EnumClipboardFormats(0); f != 0; f = EnumClipboardFormats(f)) {
        if (!formats.empty()) formats += ", ";
        formats += formatLabel(f);
    }
    if (formats.empty()) formats = "(empty)";

    bool ok = false;
    if (IsClipboardFormatAvailable(CF_ENHMETAFILE)) {
        // Windows 會把 CF_METAFILEPICT 自動合成 CF_ENHMETAFILE，兩種來源都涵蓋
        HENHMETAFILE h = static_cast<HENHMETAFILE>(GetClipboardData(CF_ENHMETAFILE));
        if (h) {
            const UINT n = GetEnhMetaFileBits(h, 0, nullptr);
            if (n > 0) {
                out.resize(n);
                ok = (GetEnhMetaFileBits(h, n, out.data()) == n);
                if (!ok) out.clear();
            }
        }
    }
    CloseClipboard();
    return ok;
}

#else   // ── 非 Windows：只有 stub ───────────────────────────────────────

bool readClipboardEmf(std::vector<unsigned char>& out, std::string& formats)
{
    out.clear();
    formats = "(EMF clipboard is only available on Windows)";
    return false;
}

#endif

} // namespace core
} // namespace aicad
