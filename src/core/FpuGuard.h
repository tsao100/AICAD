/**
 * @file FpuGuard.h
 * @brief 浮點例外遮罩工具（Windows / POSIX）
 *
 * 背景：ECL 在 cl_boot() 之後（Windows 上沒有任何還原 FPU 狀態的動作）會
 * 讓整個行程的 x87/SSE 浮點例外 trap 保持開啟（zero-divide / invalid /
 * overflow）。之後只要 C++ 端（QJSEngine、OCCT、Eigen、求解器…）任何一次
 * 運算產生 Inf/NaN —— 即使是程式本來就預期並會用 isfinite() 處理的 ——
 * 都會觸發 SIGFPE，被 ECL 轉成 Lisp 的 FLOATING-POINT-OVERFLOW 條件，
 * 而此時並不在任何 Lisp handler 內，ECL 自己的錯誤處理又失敗
 * （"GO found an inexistent tag"），行程直接死亡。
 *
 * 本檔提供：
 *   aicad::core::maskFpuExceptions()  — 立即遮蔽本執行緒所有浮點例外
 *   aicad::core::FpuMaskGuard         — RAII：進入時遮蔽，離開時還原
 */
#pragma once

#if defined(_WIN32)
#  include <float.h>
#elif defined(__unix__) || defined(__APPLE__)
#  include <fenv.h>
#endif

namespace aicad {
namespace core {

/// 遮蔽（停用）目前執行緒的所有浮點例外 trap，並清除已累積的狀態旗標。
inline void maskFpuExceptions()
{
#if defined(_WIN32)
    _clearfp();
    unsigned int cw = 0;
    // _MCW_EM 全部設為 1 = 全部例外被遮蔽（不會觸發 SIGFPE）
    _controlfp_s(&cw, _MCW_EM, _MCW_EM);
#elif defined(__unix__) || defined(__APPLE__)
    feclearexcept(FE_ALL_EXCEPT);
#  if defined(__GLIBC__)
    fedisableexcept(FE_ALL_EXCEPT);
#  endif
#endif
}

/// RAII：建構時遮蔽浮點例外，解構時還原原本的控制字。
class FpuMaskGuard {
public:
    FpuMaskGuard()
    {
#if defined(_WIN32)
        _controlfp_s(&m_saved, 0, 0);          // 讀取目前控制字
        m_valid = true;
#elif defined(__GLIBC__)
        m_saved = fegetexcept();
        m_valid = true;
#endif
        maskFpuExceptions();
    }
    ~FpuMaskGuard()
    {
        if (!m_valid) return;
#if defined(_WIN32)
        _clearfp();
        unsigned int cw = 0;
        _controlfp_s(&cw, m_saved, _MCW_EM);   // 只還原例外遮罩位元
#elif defined(__GLIBC__)
        feclearexcept(FE_ALL_EXCEPT);
        feenableexcept(m_saved);
#endif
    }
    FpuMaskGuard(const FpuMaskGuard&) = delete;
    FpuMaskGuard& operator=(const FpuMaskGuard&) = delete;

private:
#if defined(_WIN32)
    unsigned int m_saved = 0;
#elif defined(__GLIBC__)
    int m_saved = 0;
#endif
    bool m_valid = false;
};

} // namespace core
} // namespace aicad
