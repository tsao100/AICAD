/**
 * @file LispEngine.cpp
 * @brief LispEngine 類別實作
 * @author Daney
 * @date 2024-12-04
 */

#include "LispEngine.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>

#ifdef __unix__
#include <fenv.h>
#include <signal.h>
#endif

#include "../core/FpuGuard.h"

namespace aicad {
namespace scripting {

#ifdef HAVE_ECL

/**
 * @brief 函式註冊資訊
 */
struct FunctionInfo {
    QString name;
    LispCallback callback;
    int minArgs;
    int maxArgs;
};

// 全域函式註冊表 (ECL 需要靜態資料)
static QHash<QString, FunctionInfo> g_functionRegistry;
static QMutex g_registryMutex;

// ---------------------------------------------------------------------------
// QString <-> Lisp string（以 Unicode 碼位轉換，與 Windows 的 locale / 外部編碼無關）
//
// 原本用 ecl_cstring_to_base_string_or_nil(toUtf8()) 與 si_coerce_to_base_string：
// base-string 只能存 Latin-1 以內的字元，含中文的字串（草圖名稱、註解、
// (print-message "...")）在 Lisp 端會被當成一串位元組，輸出時不是亂碼就是
// "<unconvertible>"。改成直接建立 UCS-4（extended）字串。
// ---------------------------------------------------------------------------
static cl_object qStringToClString(const QString& s)
{
    const auto ucs4 = s.toUcs4();           // Qt5: QVector<uint>, Qt6: QList<uint>
    const cl_index n = static_cast<cl_index>(ucs4.size());
    cl_object str = ecl_alloc_simple_vector(n, ecl_aet_ch);
    for (cl_index i = 0; i < n; ++i) {
        ecl_char_set(str, i, static_cast<ecl_character>(ucs4[static_cast<int>(i)]));
    }
    return str;
}

static QString clStringToQString(cl_object str)
{
    const cl_index n = static_cast<cl_index>(ecl_length(str));
    QVector<uint> ucs4;
    ucs4.reserve(static_cast<int>(n));
    for (cl_index i = 0; i < n; ++i) {
        ucs4.append(static_cast<uint>(ecl_char(str, i)));
    }
    return QString::fromUcs4(ucs4.constData(), ucs4.size());
}

/// Lisp 碼位索引 → QString UTF-16 索引（只有 BMP 外的字元，如 emoji，兩者才會不同）
static int codePointIndexToUtf16(const QString& s, qint64 cpIndex)
{
    if (cpIndex <= 0) return 0;
    int i = 0;
    qint64 cp = 0;
    while (i < s.size() && cp < cpIndex) {
        if (s.at(i).isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate())
            i += 2;
        else
            ++i;
        ++cp;
    }
    return i;
}

// ---------------------------------------------------------------------------
// 啟動時載入的 Lisp 輔助程式（純 ASCII；每個元素是一個頂層表達式）
// ---------------------------------------------------------------------------
static const char* const kBootstrapForms[] = {
    // 以 double-float 讀寫浮點數。預設的 single-float 只有約 7 位有效數字：
    // (line 0 0 2700123.456 1) 這類 TM2 大座標會被默默截斷。
    "(setf *read-default-float-format* 'double-float)",

    // 嵌入式環境中 (quit)/(exit) 會直接結束整個 AICAD 行程；(break) 會進入讀 stdin
    // 的 ECL break loop（GUI 程式沒有 stdin → 卡死）。CLHS 規定 break 會把
    // *debugger-hook* 綁成 NIL，所以無法用 hook 攔截，只能在 CL-USER 中遮蔽（shadow）
    // 這幾個符號，改成丟出一般錯誤，由編輯器顯示。
    //
    // ⚠️ shadow 與 defun 必須是「兩個分開求值的表達式」：reader 會在求值前就把整個
    //    表單讀完；寫在同一個 progn 裡，cl-user::quit 會在 shadow 生效之前被解析成
    //    繼承來的 EXT:QUIT，結果等於把 ECL 自己的 quit 改掉（連它的 debugger 都壞）。
    "(shadow '(\"QUIT\" \"EXIT\" \"BREAK\") \"CL-USER\")",
    "(defun cl-user::quit (&rest args) (declare (ignore args))"
    "  (error \"quit/exit is disabled inside AICAD\"))",
    "(defun cl-user::exit (&rest args) (declare (ignore args))"
    "  (error \"quit/exit is disabled inside AICAD\"))",
    "(defun cl-user::break (&rest args) (declare (ignore args))"
    "  (error \"break is disabled inside AICAD (no interactive debugger)\"))",

    // 跳過空白、; 行註解、#| |# 區塊註解。還有資料可讀回傳 T，EOF 回傳 NIL。
    "(defun cl-user::%aicad-skip-blanks (in)"
    "  (loop"
    "    (let ((c (peek-char nil in nil nil)))"
    "      (cond ((null c) (return nil))"
    "            ((or (char= c #\\Space) (not (graphic-char-p c))) (read-char in))"
    "            ((char= c #\\;) (read-line in nil nil))"
    "            ((char= c #\\#)"
    "             (read-char in)"
    "             (let ((c2 (peek-char nil in nil nil)))"
    "               (if (and c2 (char= c2 #\\|))"
    "                   (let ((depth 1) (prev nil))"
    "                     (read-char in)"
    "                     (loop while (> depth 0)"
    "                           do (let ((x (read-char in nil nil)))"
    "                                (cond ((null x) (return))"
    "                                      ((and prev (char= prev #\\|) (char= x #\\#))"
    "                                       (decf depth) (setf prev nil))"
    "                                      ((and prev (char= prev #\\#) (char= x #\\|))"
    "                                       (incf depth) (setf prev nil))"
    "                                      (t (setf prev x))))))"
    "                   (progn (unread-char c in) (return t)))))"
    "            (t (return t))))))",

    // 依序讀取並求值 code 內所有頂層表達式。
    // 用 read-preserving-whitespace：一般的 read 會多吃掉表達式後面的一個字元，
    // file-position 就會比表達式真正的結尾多 1，錯誤標示範圍會偏掉。
    // 回傳 (ok output value error start end nforms)；start/end 為 Lisp 碼位索引。
    "(defun cl-user::%aicad-eval-string (code)"
    "  (let ((out (make-string-output-stream))"
    "        (in (make-string-input-stream code))"
    "        (nforms 0) (start 0) (end 0) (value \"\") (err nil) (phase :read))"
    "    (let ((*standard-output* out) (*error-output* out) (*trace-output* out)"
    "          (*standard-input* (make-string-input-stream \"\"))"
    "          (*package* (find-package \"CL-USER\"))"
    "          (*read-default-float-format* 'double-float)"
    "          (*print-pretty* nil) (*print-circle* t)"
    "          (*print-length* 200) (*print-level* 12)"
    "          (*debugger-hook* (lambda (c h) (declare (ignore h)) (error \"~A\" c))))"
    "      (handler-case"
    "          (loop"
    "            (unless (cl-user::%aicad-skip-blanks in) (return))"
    "            (setf start (file-position in) phase :read)"
    "            (let ((form (read-preserving-whitespace in nil in)))"
    "              (setf end (file-position in) phase :eval)"
    "              (let ((vals (multiple-value-list (eval form))))"
    "                (incf nforms)"
    "                (setf value (format nil \"~{~S~^ ; ~}\" vals)))))"
    "        (serious-condition (e)"
    "          (when (eq phase :read) (setf end (file-position in)))"
    "          (setf err (format nil \"~A: ~A\" (type-of e)"
    "                            (or (ignore-errors (princ-to-string e)) \"(no message)\"))))))"
    "    (list (null err) (get-output-stream-string out) value (or err \"\")"
    "          start end nforms)))",
};

#endif

class LispEngine::Private {
public:
    Private()
        : initialized(false)
    {
    }
    
    bool initialized;
    QString lastError;
    
#ifdef HAVE_ECL
    // 保存 FPU 狀態 (Unix)
#ifdef __unix__
    fenv_t fpuState;
    struct sigaction oldSigfpe;
#endif
#endif
};

LispEngine::LispEngine(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[LispEngine] Created";
}

LispEngine::~LispEngine() {
    qDebug() << "[LispEngine] Destroying...";
    shutdown();
    delete d;
}

bool LispEngine::initialize() {
    if (d->initialized) {
        qWarning() << "[LispEngine] Already initialized";
        return true;
    }
    
#ifdef HAVE_ECL
    qDebug() << "[LispEngine] Initializing ECL...";
    
    try {
#ifdef __unix__
        // 保存 FPU 狀態
        fegetenv(&d->fpuState);
        sigaction(SIGFPE, NULL, &d->oldSigfpe);
#endif
        
        // ⚠️ ECL 預設會安裝「整個行程」層級的 SIGSEGV/SIGBUS handler
        //    （用於它自己的 stack-overflow 偵測與 Boehm GC），不是只在真的
        //    執行 Lisp 程式碼時才生效——只要程式的任何地方（包括跟 Lisp
        //    完全無關的 Qt/OCCT 程式碼）發生真正的記憶體錯誤，都會被 ECL
        //    攔截，然後嘗試透過 Lisp 的 condition system 回報，結果往往是
        //    ECL 自己的回報機制又出錯（例如 "GO found an inexistent tag"），
        //    最後行程還是死掉，但完全看不到真正出事的 C++ 呼叫堆疊。
        //    在 cl_boot() 之前關掉這兩個攔截，讓真正的記憶體錯誤照 OS 預設
        //    行為當掉，這樣用偵錯器（gdb/Qt Creator）或 core dump 才能看到
        //    真正出問題的那一行程式碼，而不是被 ECL 的錯誤處理機制蓋掉。
        ecl_set_option(ECL_OPT_TRAP_SIGSEGV, 0);
        ecl_set_option(ECL_OPT_TRAP_SIGBUS, 0);

        // ⚠️ 同理關掉 SIGFPE：ECL 預設會開啟整個行程的浮點例外 trap
        //    （zero-divide / invalid / overflow），並把 SIGFPE 轉成 Lisp 的
        //    FLOATING-POINT-OVERFLOW 條件。但 AICAD 的 C++ 端（QJSEngine
        //    參數運算式、OCCT、Eigen、約束求解器）會合法地產生並處理
        //    Inf/NaN；在非 Lisp 呼叫堆疊中觸發時，ECL 無法正確處理
        //    （"GO found an inexistent tag"），整個行程崩潰。
        //    實際案例：開啟 FST.aicad（Sketch 1 載入完成後）即崩潰。
        ecl_set_option(ECL_OPT_TRAP_SIGFPE, 0);

        // 初始化 ECL
        char* argv[1] = {(char*)"AICAD"};
        cl_boot(1, argv);

        // Windows（MSVC/MinGW）與其他平台：cl_boot 之後無條件把本執行緒的
        // 浮點例外全部遮蔽。原本只有 __unix__ 區塊會還原 FPU 狀態，
        // Windows 完全沒有，導致 trap 維持開啟。
        aicad::core::maskFpuExceptions();

        // 載入輔助 Lisp 程式（多表達式求值 / 輸出擷取 / 錯誤攔截）
        for (const char* src : kBootstrapForms) {
            cl_eval(c_string_to_object(src));
        }
        
#ifdef __unix__
        // 恢復 FPU 狀態
        fesetenv(&d->fpuState);
        sigaction(SIGFPE, &d->oldSigfpe, NULL);
        feclearexcept(FE_ALL_EXCEPT);
#endif
        
        d->initialized = true;
        qDebug() << "[LispEngine] ECL initialized successfully";
        
        Q_EMIT initialized();
        return true;
        
    } catch (const std::exception& e) {
        d->lastError = QString("ECL initialization failed: %1").arg(e.what());
        qCritical() << "[LispEngine]" << d->lastError;
        Q_EMIT errorOccurred(d->lastError);
        return false;
    } catch (...) {
        d->lastError = "ECL initialization failed: Unknown exception";
        qCritical() << "[LispEngine]" << d->lastError;
        Q_EMIT errorOccurred(d->lastError);
        return false;
    }
#else
    d->lastError = "ECL support not compiled in";
    qWarning() << "[LispEngine]" << d->lastError;
    Q_EMIT errorOccurred(d->lastError);
    return false;
#endif
}

void LispEngine::shutdown() {
    if (!d->initialized) {
        return;
    }
    
#ifdef HAVE_ECL
    qDebug() << "[LispEngine] Shutting down ECL...";
    
    try {
        cl_shutdown();
        d->initialized = false;
        qDebug() << "[LispEngine] ECL shutdown completed";
    } catch (...) {
        qWarning() << "[LispEngine] Exception during ECL shutdown";
    }
#endif
}

bool LispEngine::isInitialized() const {
    return d->initialized;
}

QVariant LispEngine::eval(const QString& code) {
    if (!d->initialized) {
        d->lastError = "Lisp engine not initialized";
        Q_EMIT errorOccurred(d->lastError);
        return QVariant();
    }
    
#ifdef HAVE_ECL
    try {
        qDebug() << "[LispEngine] Evaluating:" << code;
        
        // 將字串轉換為 Lisp 物件
        cl_object form = c_string_to_object(code.toUtf8().constData());
        
        if (form == Cnil) {
            d->lastError = "Failed to parse Lisp code";
            Q_EMIT errorOccurred(d->lastError);
            return QVariant();
        }
        
        // 執行（Lisp 執行期間也確保浮點例外維持遮蔽，結束後還原）
        aicad::core::FpuMaskGuard fpuGuard;
        cl_object result = Cnil;
        
#ifdef _MSC_VER
        result = cl_eval(form);
#else
        CL_CATCH_ALL_BEGIN(ecl_process_env()) {
            result = cl_eval(form);
        } CL_CATCH_ALL_IF_CAUGHT {
            d->lastError = "Error evaluating Lisp expression";
            qWarning() << "[LispEngine]" << d->lastError;
            Q_EMIT errorOccurred(d->lastError);
            return QVariant();
        } CL_CATCH_ALL_END;
#endif
        
        // 轉換結果
        QVariant qResult = clObjectToQVariant(result);
        
        qDebug() << "[LispEngine] Result:" << qResult;
        
        return qResult;
        
    } catch (const std::exception& e) {
        d->lastError = QString("Evaluation error: %1").arg(e.what());
        qWarning() << "[LispEngine]" << d->lastError;
        Q_EMIT errorOccurred(d->lastError);
        return QVariant();
    } catch (...) {
        d->lastError = "Unknown evaluation error";
        qWarning() << "[LispEngine]" << d->lastError;
        Q_EMIT errorOccurred(d->lastError);
        return QVariant();
    }
#else
    Q_UNUSED(code);
    d->lastError = "ECL support not compiled in";
    Q_EMIT errorOccurred(d->lastError);
    return QVariant();
#endif
}

LispEvalResult LispEngine::evalDetailed(const QString& code) {
    LispEvalResult r;

    if (!d->initialized) {
        r.error = QStringLiteral("Lisp engine not initialized");
        d->lastError = r.error;
        return r;
    }

#ifdef HAVE_ECL
    aicad::core::FpuMaskGuard fpuGuard;

    // 所有 Lisp 錯誤都在 %AICAD-EVAL-STRING 內以 handler-case 攔下；
    // 這裡不會有 non-local exit 穿過 C++ 堆疊。
    cl_object fn  = c_string_to_object("CL-USER::%AICAD-EVAL-STRING");
    cl_object res = cl_funcall(2, fn, qStringToClString(code));

    if (res == Cnil || !ECL_LISTP(res)) {
        r.error = QStringLiteral("Internal error: evaluator returned no result");
        d->lastError = r.error;
        return r;
    }

    // (ok output value error start end nforms)
    r.ok     = (ecl_nth(0, res) != Cnil);
    r.output = clStringToQString(ecl_nth(1, res));
    r.value  = clStringToQString(ecl_nth(2, res));
    r.error  = clStringToQString(ecl_nth(3, res));
    r.formsEvaluated = static_cast<int>(ecl_fixnum(ecl_nth(6, res)));

    if (!r.ok) {
        r.errorStart = codePointIndexToUtf16(code, ecl_fixnum(ecl_nth(4, res)));
        r.errorEnd   = codePointIndexToUtf16(code, ecl_fixnum(ecl_nth(5, res)));
        if (r.errorEnd < r.errorStart) r.errorEnd = r.errorStart;
        d->lastError = r.error;
    } else {
        d->lastError.clear();
    }
    return r;
#else
    Q_UNUSED(code);
    r.error = QStringLiteral("ECL support not compiled in");
    d->lastError = r.error;
    return r;
#endif
}

bool LispEngine::loadFile(const QString& filePath) {
    if (!d->initialized) {
        d->lastError = "Lisp engine not initialized";
        Q_EMIT errorOccurred(d->lastError);
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        d->lastError = QString("Cannot open file: %1").arg(filePath);
        qWarning() << "[LispEngine]" << d->lastError;
        Q_EMIT errorOccurred(d->lastError);
        return false;
    }
    
    qDebug() << "[LispEngine] Loading file:" << filePath;
    
    // 一律以 UTF-8 解讀（腳本內常有中文註解 / 字串），並去掉 BOM。
    QString code = QString::fromUtf8(file.readAll());
    file.close();
    if (code.startsWith(QChar(0xFEFF))) {
        code.remove(0, 1);
    }
    
    // 舊版用 eval()，只會執行檔案中的第一個表達式；改為依序執行全部。
    const LispEvalResult r = evalDetailed(code);
    
    if (!r.output.isEmpty()) {
        Q_EMIT output(r.output);
    }
    
    if (!r.ok) {
        qWarning() << "[LispEngine] Load failed:" << filePath << r.error;
        Q_EMIT errorOccurred(r.error);
        return false;
    }
    
    qDebug() << "[LispEngine] File loaded successfully:" << filePath
             << "forms:" << r.formsEvaluated;
    return true;
}

void LispEngine::registerFunction(const QString& name,
                                 LispCallback callback,
                                 int minArgs,
                                 int maxArgs)
{
    if (!d->initialized) {
        qWarning() << "[LispEngine] Cannot register function - not initialized";
        return;
    }
    
#ifdef HAVE_ECL
    qDebug() << "[LispEngine] Registering function:" << name
             << "minArgs:" << minArgs << "maxArgs:" << maxArgs;
    
    try {
        // 註冊到全域表
        {
            QMutexLocker locker(&g_registryMutex);
            
            FunctionInfo info;
            info.name = name;
            info.callback = callback;
            info.minArgs = minArgs;
            info.maxArgs = maxArgs;
            
            g_functionRegistry[name.toUpper()] = info;
        }
        
        // 在 ECL 中註冊
        cl_object symbol = ecl_make_symbol(name.toUtf8().constData(), "CL-USER");
        
        // 使用可變參數函式
        ecl_def_c_function_va(symbol, (cl_objectfn)eclFunctionWrapper, 0);
        
        qDebug() << "[LispEngine] Function registered successfully:" << name;
        
    } catch (const std::exception& e) {
        qWarning() << "[LispEngine] Error registering function:" << name
                   << "-" << e.what();
    } catch (...) {
        qWarning() << "[LispEngine] Unknown error registering function:" << name;
    }
#else
    Q_UNUSED(name);
    Q_UNUSED(callback);
    Q_UNUSED(minArgs);
    Q_UNUSED(maxArgs);
#endif
}

void LispEngine::setVariable(const QString& name, const QVariant& value) {
    if (!d->initialized) {
        return;
    }
    
#ifdef HAVE_ECL
    try {
        cl_object symbol = ecl_make_symbol(name.toUtf8().constData(), "CL-USER");
        cl_object clValue = qVariantToClObject(value);
        cl_set(symbol, clValue);
        
        qDebug() << "[LispEngine] Variable set:" << name << "=" << value;
        
    } catch (...) {
        qWarning() << "[LispEngine] Error setting variable:" << name;
    }
#else
    Q_UNUSED(name);
    Q_UNUSED(value);
#endif
}

QVariant LispEngine::getVariable(const QString& name) {
    if (!d->initialized) {
        return QVariant();
    }
    
#ifdef HAVE_ECL
    try {
        cl_object symbol = ecl_make_symbol(name.toUtf8().constData(), "CL-USER");
        cl_object value = cl_symbol_value(symbol);
        
        return clObjectToQVariant(value);
        
    } catch (...) {
        qWarning() << "[LispEngine] Error getting variable:" << name;
        return QVariant();
    }
#else
    Q_UNUSED(name);
    return QVariant();
#endif
}

QString LispEngine::lastError() const {
    return d->lastError;
}

#ifdef HAVE_ECL

QVariant LispEngine::clObjectToQVariant(cl_object obj) {
    if (obj == Cnil) {
        return QVariant();
    }
    
    // 數字
    if (ECL_FIXNUMP(obj)) {
        return QVariant::fromValue(ecl_fixnum(obj));
    }
    
    if (ECL_SINGLE_FLOAT_P(obj)) {
        return QVariant::fromValue(ecl_single_float(obj));
    }
    
    if (ECL_DOUBLE_FLOAT_P(obj)) {
        return QVariant::fromValue(ecl_double_float(obj));
    }
    
    // 字串
    if (ECL_STRINGP(obj)) {
        return QVariant::fromValue(clObjectToQString(obj));
    }
    
    // 列表
    if (ECL_LISTP(obj)) {
        QVariantList list;
        
        while (obj != Cnil) {
            list.append(clObjectToQVariant(ecl_car(obj)));
            obj = ecl_cdr(obj);
        }
        
        return QVariant::fromValue(list);
    }
    
    // 其他：轉為字串
    return QVariant::fromValue(clObjectToQString(obj));
}

cl_object LispEngine::qVariantToClObject(const QVariant& var) {
    if (!var.isValid()) {
        return Cnil;
    }
    
    // userType()：Qt5／Qt6 皆可用；QVariant::type()／QVariant::Type 在 Qt6 已棄用
    switch (var.userType()) {
    case QMetaType::Bool:
        return var.toBool() ? Ct : Cnil;
        
    case QMetaType::Int:
        return ecl_make_integer(var.toInt());
        
    case QMetaType::LongLong:
        return ecl_make_int64_t(var.toLongLong());
        
    case QMetaType::Double:
        return ecl_make_double_float(var.toDouble());
        
    case QMetaType::QString:
        return qStringToClString(var.toString());
        
    case QMetaType::QVariantList: {
        QVariantList list = var.toList();
        cl_object result = Cnil;
        
        // 反向建立列表
        for (int i = list.size() - 1; i >= 0; --i) {
            cl_object elem = qVariantToClObject(list[i]);
            result = CONS(elem, result);
        }
        
        return result;
    }
        
    default:
        // 其他類型轉為字串
        return ecl_cstring_to_base_string_or_nil(var.toString().toUtf8().constData());
    }
}

QString LispEngine::clObjectToQString(cl_object obj) {
    if (obj == Cnil) {
        return "NIL";
    }
    
    if (obj == Ct) {
        return "T";
    }
    
    // 字串物件直接以碼位轉換；其他物件先 princ-to-string 再轉換
    // （不再經過 base-string，含中文的字串才不會變成 "<unconvertible>"）
    if (ECL_STRINGP(obj)) {
        return clStringToQString(obj);
    }
    
    cl_object strObj = cl_princ_to_string(obj);
    
    if (strObj != Cnil && ECL_STRINGP(strObj)) {
        return clStringToQString(strObj);
    }
    
    return "<unconvertible>";
}

cl_object LispEngine::eclFunctionWrapper(cl_narg narg, ...) {
    va_list args;
    va_start(args, narg);

    // 取得函式名稱。
    // 舊版程式碼使用的 ecl_current_function() / ecl_function_name()
    // 在任何公開版本的 ECL 中均不存在（object.h 也找不到這兩個符號）。
    // 正確做法：
    //   1. 透過 ecl_process_env() 取得目前執行緒的 ECL 環境指標。
    //   2. env->function 即為目前正在被呼叫的函式物件（t_cfun 或
    //      t_cfunfixed，由 ecl_def_c_function_va 建立）。
    //   3. si_compiled_function_name(fn) 回傳該物件的 .cfun.name，
    //      是在 ecl_def_c_function_va 綁定 symbol 時自動填入的 symbol name。
    cl_env_ptr the_env = ecl_process_env();
    cl_object  fn      = the_env->function;
    cl_object  nameObj = (fn != Cnil) ? si_compiled_function_name(fn) : Cnil;
    QString fnName = clObjectToQString(nameObj).toUpper();
    
    qDebug() << "[LispEngine] Calling:" << fnName << "with" << narg << "args";
    
    // 查找註冊的函式
    FunctionInfo info;
    {
        QMutexLocker locker(&g_registryMutex);
        
        if (!g_functionRegistry.contains(fnName)) {
            va_end(args);
            qWarning() << "[LispEngine] Function not found:" << fnName;
            return Cnil;
        }
        
        info = g_functionRegistry[fnName];
    }
    
    // 檢查參數數量
    if (narg < info.minArgs) {
        va_end(args);
        qWarning() << "[LispEngine]" << fnName << "requires at least"
                   << info.minArgs << "arguments, got" << narg;
        return Cnil;
    }
    
    if (info.maxArgs >= 0 && narg > info.maxArgs) {
        va_end(args);
        qWarning() << "[LispEngine]" << fnName << "accepts at most"
                   << info.maxArgs << "arguments, got" << narg;
        return Cnil;
    }
    
    // 轉換參數
    QVariantList qArgs;
    for (int i = 0; i < narg; ++i) {
        cl_object arg = va_arg(args, cl_object);
        qArgs.append(clObjectToQVariant(arg));
    }
    va_end(args);
    
    // 呼叫 C++ 回呼
    try {
        QVariant result = info.callback(qArgs);
        return qVariantToClObject(result);
        
    } catch (const std::exception& e) {
        qWarning() << "[LispEngine] Exception in" << fnName << ":" << e.what();
        return Cnil;
    } catch (...) {
        qWarning() << "[LispEngine] Unknown exception in" << fnName;
        return Cnil;
    }
}

#endif

} // namespace scripting
} // namespace aicad