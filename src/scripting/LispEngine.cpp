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
        
        // 初始化 ECL
        char* argv[1] = {(char*)"AICAD"};
        cl_boot(1, argv);
        
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
        
        // 執行
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

bool LispEngine::loadFile(const QString& filePath) {
    if (!d->initialized) {
        d->lastError = "Lisp engine not initialized";
        Q_EMIT errorOccurred(d->lastError);
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        d->lastError = QString("Cannot open file: %1").arg(filePath);
        qWarning() << "[LispEngine]" << d->lastError;
        Q_EMIT errorOccurred(d->lastError);
        return false;
    }
    
    qDebug() << "[LispEngine] Loading file:" << filePath;
    
    QTextStream in(&file);
    QString code = in.readAll();
    file.close();
    
    QVariant result = eval(code);
    
    if (!d->lastError.isEmpty()) {
        return false;
    }
    
    qDebug() << "[LispEngine] File loaded successfully:" << filePath;
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
    
    switch (var.type()) {
    case QVariant::Bool:
        return var.toBool() ? Ct : Cnil;
        
    case QVariant::Int:
        return ecl_make_integer(var.toInt());
        
    case QVariant::LongLong:
        return ecl_make_int64_t(var.toLongLong());
        
    case QVariant::Double:
        return ecl_make_double_float(var.toDouble());
        
    case QVariant::String:
        return ecl_cstring_to_base_string_or_nil(var.toString().toUtf8().constData());
        
    case QVariant::List: {
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
    
    // 使用 princ-to-string
    cl_object strObj = cl_princ_to_string(obj);
    
    if (strObj != Cnil && ECL_STRINGP(strObj)) {
        if (ECL_BASE_STRING_P(strObj)) {
            const char* cstr = (const char*)ecl_base_string_pointer_safe(strObj);
            if (cstr) {
                return QString::fromUtf8(cstr);
            }
        } else {
            // 轉換為 base-string
            cl_object baseStr = si_coerce_to_base_string(strObj);
            if (baseStr != Cnil && ECL_BASE_STRING_P(baseStr)) {
                const char* cstr = (const char*)ecl_base_string_pointer_safe(baseStr);
                if (cstr) {
                    return QString::fromUtf8(cstr);
                }
            }
        }
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