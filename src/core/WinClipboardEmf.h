/**
 * @file WinClipboardEmf.h
 * @brief 從 Windows 剪貼簿取出 Enhanced Metafile (EMF) 位元組。
 *
 * 獨立成一個小檔案的原因：<windows.h> 會汙染全域巨集（min/max/interface…），
 * 不應該被 include 進 Command.h / OCCT 標頭一堆的命令實作檔。
 * 非 Windows 平台只有 stub（一律回傳 false 並說明原因）。
 */
#ifndef AICAD_CORE_WINCLIPBOARDEMF_H
#define AICAD_CORE_WINCLIPBOARDEMF_H

#include <string>
#include <vector>

namespace aicad {
namespace core {

/**
 * @param out      成功時填入 EMF 原始位元組
 * @param formats  不論成敗，都填入剪貼簿目前有哪些格式（診斷用，例如
 *                 "AutoCAD.r18, Enhanced Metafile, DIB"）
 * @return true 表示取得 EMF
 */
bool readClipboardEmf(std::vector<unsigned char>& out, std::string& formats);

} // namespace core
} // namespace aicad

#endif
