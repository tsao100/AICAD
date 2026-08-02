/**
 * @file LispEngine.h
 * @brief Lisp 引擎核心，管理 ECL Lisp 環境
 * @author Daney
 * @date 2024-12-04
 */

#ifndef AICAD_SCRIPTING_LISPENGINE_H
#define AICAD_SCRIPTING_LISPENGINE_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QHash>
#include <functional>

#ifdef HAVE_ECL
// ECL pulls in ecl/gmp.h which contains mpz_get_ui() returning mp_limb_t
// (unsigned long long on Win64) as unsigned long, triggering MSVC C4244.
// Suppress at the include boundary since this is a third-party header.
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable: 4244)  // 'return': conversion from mp_limb_t to unsigned long
#endif

// <ecl/object.h> 內含名為 slots 的結構成員（cl_object *slots;）。由於本檔案
// 上方已經 #include <QObject>，Qt 會把 slots / signals / emit 展開成巨集
// （slots 會展開成空字串），使得該行被展開成 "cl_object *;"，導致
// 「expected unqualified-id before ';' token」的編譯錯誤
// （錯誤發生在 ecl/object.h:1049，但根源是本檔案的 include 順序）。
// 用 push_macro/undef/pop_macro 在 include ECL 標頭期間暫時清除這三個巨集，
// include 完畢後立刻還原，讓本檔案不論被哪個順序 include 都能正確編譯，
// 也不需要每個使用端（main.cpp、LispBindings.cpp…）各自維護
// 「必須在 Qt 標頭之前 include ECL」的脆弱約定。
#pragma push_macro("slots")
#pragma push_macro("SLOT")
#pragma push_macro("signals")
#pragma push_macro("emit")
#undef slots
#undef SLOT
#undef signals
#undef emit

#include <ecl/ecl.h>

#pragma pop_macro("emit")
#pragma pop_macro("signals")
#pragma pop_macro("SLOT")
#pragma pop_macro("slots")

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
#endif

namespace aicad {
namespace scripting {

/**
 * @brief Lisp 函式回呼類型
 * 
 * 參數是 QVariantList，返回值是 QVariant
 */
using LispCallback = std::function<QVariant(const QVariantList&)>;

/**
 * @brief Lisp 引擎
 * 
 * LispEngine 提供：
 * - ECL Lisp 環境初始化
 * - 執行 Lisp 代碼
 * - 註冊 C++ 函式到 Lisp
 * - 型別轉換 (Lisp ↔ Qt)
 * 
 * 使用範例：
 * @code
 * LispEngine* lisp = app->lispEngine();
 * 
 * // 註冊函式
 * lisp->registerFunction("create-line", [](const QVariantList& args) {
 *     // 實作...
 *     return QVariant();
 * });
 * 
 * // 執行代碼
 * QVariant result = lisp->eval("(+ 1 2 3)");
 * @endcode
 */
class LispEngine : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief 建構子
     * @param parent 父物件
     */
    explicit LispEngine(QObject* parent = nullptr);
    
    /**
     * @brief 解構子
     */
    ~LispEngine() override;
    
    /**
     * @brief 初始化 Lisp 環境
     * @return 成功回傳 true
     */
    bool initialize();
    
    /**
     * @brief 關閉 Lisp 環境
     */
    void shutdown();
    
    /**
     * @brief 檢查是否已初始化
     */
    bool isInitialized() const;
    
    /**
     * @brief 執行 Lisp 代碼
     * @param code Lisp 代碼字串
     * @return 執行結果
     */
    QVariant eval(const QString& code);
    
    /**
     * @brief 載入 Lisp 檔案
     * @param filePath 檔案路徑
     * @return 成功回傳 true
     */
    bool loadFile(const QString& filePath);
    
    /**
     * @brief 註冊 C++ 函式到 Lisp
     * @param name Lisp 中的函式名稱
     * @param callback C++ 回呼函式
     * @param minArgs 最小參數數量
     * @param maxArgs 最大參數數量 (-1 表示不限)
     * 
     * @note 函式會註冊在 CL-USER 套件中
     */
    void registerFunction(const QString& name,
                         LispCallback callback,
                         int minArgs = 0,
                         int maxArgs = -1);
    
    /**
     * @brief 設定 Lisp 變數
     * @param name 變數名稱
     * @param value 變數值
     */
    void setVariable(const QString& name, const QVariant& value);
    
    /**
     * @brief 取得 Lisp 變數
     * @param name 變數名稱
     * @return 變數值
     */
    QVariant getVariable(const QString& name);
    
    /**
     * @brief 取得最後的錯誤訊息
     */
    QString lastError() const;
    
#ifdef HAVE_ECL
    /**
     * @brief 轉換 cl_object 到 QVariant
     */
    static QVariant clObjectToQVariant(cl_object obj);
    
    /**
     * @brief 轉換 QVariant 到 cl_object
     */
    static cl_object qVariantToClObject(const QVariant& var);
    
    /**
     * @brief 轉換 cl_object 到 QString
     */
    static QString clObjectToQString(cl_object obj);
#endif
    
Q_SIGNALS:
    /**
     * @brief 初始化完成時發出
     */
    void initialized();
    
    /**
     * @brief 執行代碼時發出 (用於輸出)
     * @param text 輸出文字
     */
    void output(const QString& text);
    
    /**
     * @brief 錯誤發生時發出
     * @param message 錯誤訊息
     */
    void errorOccurred(const QString& message);
    
private:
    class Private;
    Private* d;
    
#ifdef HAVE_ECL
    /**
     * @brief ECL 函式包裝器 (C 回呼)
     */
    static cl_object eclFunctionWrapper(cl_narg narg, ...);
#endif
};

} // namespace scripting
} // namespace aicad

#endif // AICAD_SCRIPTING_LISPENGINE_H
