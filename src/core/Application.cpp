/**
 * @file Application.cpp
 * @brief Application 類別實作
 * @author Jack
 * @date 2024-12-04
 */

#include "Application.h"
#include "EventBus.h"
#include "DocumentManager.h"
#include "cad/Document.h"
#include "command/CommandManager.h"
#include "command/RectangleCommand.h"
#include "ui/UIManager.h"
#include "view/ViewManager.h"
#include "scripting/LispEngine.h"
#include "scripting/LispBindings.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

namespace aicad {
namespace core {

// Private implementation
class Application::Private {
public:
    Private()
        : initialized(false)
        , eventBus(nullptr)
        , documentManager(nullptr)
        , commandManager(nullptr)
        , uiManager(nullptr)
        , viewManager(nullptr)
        , lispEngine(nullptr)
    {
    }
    
    ~Private() {
        delete lispEngine;
        delete viewManager;
        delete uiManager;
        delete commandManager;
        delete documentManager;
        delete eventBus;
    }
    
    bool initialized;
    EventBus* eventBus;
    DocumentManager* documentManager;
    command::CommandManager* commandManager;
    ui::UIManager* uiManager;
    view::ViewManager* viewManager;
    scripting::LispEngine* lispEngine;

    static const QString VERSION;
    static const QString APP_NAME;
};

const QString Application::Private::VERSION = "1.0.0-dev";
const QString Application::Private::APP_NAME = "AICAD";

// Singleton instance
static QMutex s_mutex;
static Application* s_instance = nullptr;

Application::Application()
    : QObject(nullptr)
    , d(new Private())
{
    qDebug() << "[Application] Constructor called";
}

Application::~Application() {
    qDebug() << "[Application] Destructor called";
    shutdown();
    delete d;
}

Application* Application::instance() {
    if (s_instance == nullptr) {
        QMutexLocker locker(&s_mutex);
        if (s_instance == nullptr) {
            s_instance = new Application();
        }
    }
    return s_instance;
}

bool Application::initialize() {
    if (d->initialized) {
        qWarning() << "[Application] Already initialized";
        return true;
    }

    qDebug() << "[Application] Initializing" << d->APP_NAME << d->VERSION;

    try {
        // 1. 建立事件總線
        qDebug() << "[Application] Creating EventBus...";
        d->eventBus = new EventBus(this);
        if (!d->eventBus) {
            Q_EMIT errorOccurred("Failed to create EventBus");
            return false;
        }

        // 2. 建立文件管理器
        qDebug() << "[Application] Creating DocumentManager...";
        d->documentManager = new DocumentManager(this);
        if (!d->documentManager) {
            Q_EMIT errorOccurred("Failed to create DocumentManager");
            return false;
        }

        // 3. 建立命令管理器
        qDebug() << "[Application] Creating CommandManager...";
        d->commandManager = new command::CommandManager(this);
        if (!d->commandManager) {
            Q_EMIT errorOccurred("Failed to create CommandManager");
            return false;
        }

        // 4. 建立視圖管理器
        qDebug() << "[Application] Creating ViewManager...";
        d->viewManager = new view::ViewManager(this);
        if (!d->viewManager) {
            Q_EMIT errorOccurred("Failed to create ViewManager");
            return false;
        }

        // 5. 建立 UI 管理器
        qDebug() << "[Application] Creating UIManager...";
        d->uiManager = new ui::UIManager(this);
        if (!d->uiManager) {
            Q_EMIT errorOccurred("Failed to create UIManager");
            return false;
        }

        // 6. 初始化 UI 系統
        qDebug() << "[Application] Initializing UI system...";
        if (!d->uiManager->initialize()) {
            Q_EMIT errorOccurred("Failed to initialize UI system");
            return false;
        }

        // 7. 建立 Lisp 引擎
        qDebug() << "[Application] Creating LispEngine...";
        d->lispEngine = new scripting::LispEngine(this);
        if (!d->lispEngine) {
            Q_EMIT errorOccurred("Failed to create LispEngine");
            return false;
        }

        // 8. 初始化 Lisp 引擎
        qDebug() << "[Application] Initializing LispEngine...";
        if (!d->lispEngine->initialize()) {
            qWarning() << "[Application] LispEngine initialization failed (non-critical)";
            // 不是致命錯誤,繼續
        } else {
            // 9. 註冊 Lisp 綁定
            qDebug() << "[Application] Registering Lisp bindings...";
            scripting::LispBindings* bindings =
                new scripting::LispBindings(d->lispEngine, this, this);
            bindings->registerAll();
        }

        // 10. 連接文件管理器信號
        connectDocumentManagerSignals();

        // 11. 註冊預設命令
        registerDefaultCommands();

        d->initialized = true;
        qDebug() << "[Application] Initialization completed successfully";

        Q_EMIT initialized();
        return true;

    } catch (const std::exception& e) {
        QString error = QString("Initialization failed: %1").arg(e.what());
        qCritical() << "[Application]" << error;
        Q_EMIT errorOccurred(error);
        return false;
    } catch (...) {
        QString error = "Initialization failed: Unknown exception";
        qCritical() << "[Application]" << error;
        Q_EMIT errorOccurred(error);
        return false;
    }
}

void Application::connectDocumentManagerSignals() {
    connect(d->documentManager, &DocumentManager::documentCreated,
            this, [this](cad::Document* doc) {
                if (doc) {
                    qDebug() << "[Application] Document created:" << doc->fileName();
                    d->eventBus->publish(Events::DOCUMENT_CREATED,
                                         QVariant::fromValue(doc));
                }
            });

    connect(d->documentManager, &DocumentManager::documentOpened,
            this, [this](cad::Document* doc) {
                if (doc) {
                    qDebug() << "[Application] Document opened:" << doc->fileName();
                    d->eventBus->publish(Events::DOCUMENT_OPENED,
                                         QVariant::fromValue(doc));
                }
            });

    connect(d->documentManager, &DocumentManager::documentClosed,
            this, [this](const QString& fileName) {
                qDebug() << "[Application] Document closed:" << fileName;
                d->eventBus->publish(Events::DOCUMENT_CLOSED, fileName);
            });
}

void Application::registerDefaultCommands() {
    using namespace command;

    // 註冊矩形命令
    d->commandManager->registerCommand("rectangle", {"rect"},
                                       []() { return new command::RectangleCommand(); });

    // 註冊直線命令 (待實作)
    // d->commandManager->registerCommand("line", {"l"},
    //     []() { return new commands::LineCommand(); });

    // 註冊圓形命令 (待實作)
    // d->commandManager->registerCommand("circle", {"c"},
    //     []() { return new commands::CircleCommand(); });

    qDebug() << "[Application] Default commands registered";
}

// 添加 getter 方法
ui::UIManager* Application::uiManager() const {
    return d->uiManager;
}

view::ViewManager* Application::viewManager() const {
    return d->viewManager;
}

scripting::LispEngine* Application::lispEngine() const {
    return d->lispEngine;
}

command::CommandManager* Application::commandManager() const {
    return d->commandManager;
}

// 槽函數實作
void Application::onDocumentCreated(const QString& name) {
    qDebug() << "[Application] Document created:" << name;
    if (d->eventBus) {
        d->eventBus->publish(Events::DOCUMENT_CREATED, name);
    }
}

// ui::UIManager* Application::uiManager() const {
//     return d->uiManager;
// }

// scripting::LispEngine* Application::lispEngine() const {
//     return d->lispEngine;
// }


void Application::shutdown() {
    if (!d->initialized) {
        return;
    }
    
    qDebug() << "[Application] Shutting down...";
    
    Q_EMIT aboutToQuit();
    
    // 取消當前執行的命令
    // if (d->commandManager) {
    //     qDebug() << "[Application] Cancelling current command...";
    //     d->commandManager->cancelCurrentCommand();
    // }
    
    // 關閉所有文件
    if (d->documentManager) {
        qDebug() << "[Application] Closing all documents...";
        d->documentManager->closeAll();
    }
    
    // 清理 OCCT 資源 (暫略)
    qDebug() << "[Application] Cleaning up OCCT resources...";
    // TODO: 實際的 OCCT 清理
    
    d->initialized = false;
    qDebug() << "[Application] Shutdown completed";
}

DocumentManager* Application::documentManager() const {
    return d->documentManager;
}

EventBus* Application::eventBus() const {
    return d->eventBus;
}

// CommandManager* Application::commandManager() const {
//     return d->commandManager;
// }

bool Application::isInitialized() const {
    return d->initialized;
}

QString Application::version() const {
    return d->VERSION;
}

QString Application::applicationName() const {
    return d->APP_NAME;
}

} // namespace core
} // namespace aicad
