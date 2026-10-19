#pragma once
// 最後一道防線：Qt 事件分派（槽函式、QTimer lambda、paintEvent …）中拋出、
// 且沒有人接住的例外，預設會一路穿出事件迴圈 → std::terminate →「程式當掉」。
// 這裡在 QApplication::notify() 攔下 OCCT（Standard_Failure）與 C++ 標準例外，
// 記錄後讓程式繼續跑。
//
// 注意：這只是保險，不是修正。被攔下的例外代表某個功能這一次沒做完（log 會印出
// 事件接收者的類別與例外訊息），根因仍應在該處修正。
// 純標頭檔、沒有 Q_OBJECT，不需要改 .pro。
#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <Standard_Failure.hxx>
#include <exception>

namespace aicad::core {

class SafeApplication : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override {
        try {
            return QApplication::notify(receiver, event);
        } catch (const Standard_Failure& e) {
            report(receiver, event, QString::fromUtf8(e.DynamicType()->Name()),
                   QString::fromUtf8(e.GetMessageString() ? e.GetMessageString() : ""));
        } catch (const std::exception& e) {
            report(receiver, event, QStringLiteral("std::exception"),
                   QString::fromUtf8(e.what()));
        }
        // 不攔 catch(...)：強制展開（forced unwind）等非錯誤例外不該被吞掉。
        return false;
    }

private:
    static void report(QObject* r, QEvent* ev, const QString& type, const QString& what) {
        qCritical().noquote()
            << "[SafeApplication] Uncaught exception in event dispatch —"
            << type << ":" << what
            << "| receiver =" << (r ? r->metaObject()->className() : "null")
            << (r && !r->objectName().isEmpty() ? "(" + r->objectName() + ")" : QString())
            << "| event type =" << (ev ? int(ev->type()) : -1);
    }
};

} // namespace aicad::core
