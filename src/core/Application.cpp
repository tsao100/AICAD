/**
 * @file Application.cpp
 * @brief Application 類別實作
 * @author Jack
 * @date 2024-12-04
 */

#include "Application.h"
#include "EventBus.h"
#include "DocumentManager.h"
#include "core/command/CommandManager.h"
#include "cad/commands/RectangleCommand.h"

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
    {
    }
    
    ~Private() {
        delete documentManager;
        delete eventBus;
    }
    
    bool initialized;
    EventBus* eventBus;
    DocumentManager* documentManager;
    CommandManager* commandManager;
    
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

        // ★ 3. CommandManager（Kaufen）
        qDebug() << "[Application] Creating CommandManager...";
        d->commandManager = new CommandManager(this);
        if (!d->commandManager) {
            Q_EMIT errorOccurred("Failed to create CommandManager");
            return false;
        }

        // ★ 4. 註冊 Commands
        qDebug() << "[Application] Registering commands...";
        d->commandManager->registerCommand(
            "rectangle",
            { "rect", "rec" },
            []() {
                return new aicad::cad::RectangleCommand();
            }
            );

        // 5. 初始化 OCCT 環境 (樁函式)
        qDebug() << "[Application] Initializing OCCT environment...";
        // TODO: 實際的 OCCT 初始化
        
        // 6. 連接信號
        connect(d->documentManager, &DocumentManager::documentCreated,
                this, [this](const QString& name) {
            qDebug() << "[Application] Document created:" << name;
            d->eventBus->publish(Events::DOCUMENT_CREATED, name);
        });
        
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

void Application::shutdown() {
    if (!d->initialized) {
        return;
    }
    
    qDebug() << "[Application] Shutting down...";
    
    Q_EMIT aboutToQuit();
    
    // 關閉所有文件
    if (d->documentManager) {
        qDebug() << "[Application] Closing all documents...";
        d->documentManager->closeAll();
    }
    
    // 清理 OCCT 資源 (樁函式)
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
