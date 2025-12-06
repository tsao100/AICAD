// src/core/Application.cpp

#include "Application.h"
#include "EventBus.h"
#include "PluginManager.h"
#include "../cad/DocumentManager.h"
#include "../commands/CommandManager.h"
#include "../view/ViewManager.h"
#include "../scripting/LispEngine.h"

#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QDateTime>

namespace aicad {
namespace core {

// 版本資訊
#define AICAD_VERSION "1.0.0"
#define AICAD_BUILD_DATE __DATE__

// 靜態成員初始化
Application* Application::s_instance = nullptr;

/**
 * @brief Application 的私有實作類別 (PIMPL 模式)
 */
class Application::Private {
public:
    Private()
        : initialized(false)
        , eventBus(nullptr)
        , documentManager(nullptr)
        , commandManager(nullptr)
        , viewManager(nullptr)
        , lispEngine(nullptr)
        , pluginManager(nullptr)
        , settings(nullptr)
    {
    }
    
    ~Private() {
        // 清理資源 (按照相反順序)
        delete pluginManager;
        delete lispEngine;
        delete viewManager;
        delete commandManager;
        delete documentManager;
        delete eventBus;
        delete settings;
    }
    
    bool initialized;
    
    // 子系統
    EventBus* eventBus;
    cad::DocumentManager* documentManager;
    commands::CommandManager* commandManager;
    view::ViewManager* viewManager;
    scripting::LispEngine* lispEngine;
    PluginManager* pluginManager;
    
    // 設定
    QSettings* settings;
    QString settingsPath;
};

Application::Application()
    : QObject(nullptr)
    , d(new Private())
{
    qDebug() << "[Application] Creating instance";
}

Application::~Application() {
    qDebug() << "[Application] Destroying instance";
    shutdown();
    delete d;
}

Application* Application::instance() {
    if (s_instance == nullptr) {
        s_instance = new Application();
    }
    return s_instance;
}

bool Application::initialize() {
    if (d->initialized) {
        qWarning() << "[Application] Already initialized";
        return true;
    }
    
    qDebug() << "[Application] Initializing AICAD" << version();
    qDebug() << "[Application] Build date:" << buildDate();
    qDebug() << "[Application] Build info:" << buildInfo();
    
    // 1. 載入設定
    loadSettings();
    
    // 2. 初始化事件總線 (最先初始化，其他模組需要使用)
    if (!initializeEventBus()) {
        qCritical() << "[Application] Failed to initialize EventBus";
        return false;
    }
    
    // 3. 初始化文件管理器
    if (!initializeDocumentManager()) {
        qCritical() << "[Application] Failed to initialize DocumentManager";
        return false;
    }
    
    // 4. 初始化命令管理器
    if (!initializeCommandManager()) {
        qCritical() << "[Application] Failed to initialize CommandManager";
        return false;
    }
    
    // 5. 初始化視圖管理器
    if (!initializeViewManager()) {
        qCritical() << "[Application] Failed to initialize ViewManager";
        return false;
    }
    
    // 6. 初始化 Lisp 引擎 (可選)
    bool enableLisp = setting("scripting/enableLisp", true).toBool();
    if (enableLisp) {
        if (!initializeLispEngine()) {
            qWarning() << "[Application] Failed to initialize LispEngine (non-critical)";
        }
    }
    
    // 7. 初始化外掛管理器
    if (!initializePluginManager()) {
        qWarning() << "[Application] Failed to initialize PluginManager (non-critical)";
    }
    
    // 8. 連接子系統
    connectSubsystems();
    
    d->initialized = true;
    
    qDebug() << "[Application] Initialization complete";
    Q_EMIT initialized();
    
    return true;
}

void Application::shutdown() {
    if (!d->initialized) {
        return;
    }
    
    qDebug() << "[Application] Shutting down";
    
    Q_EMIT aboutToQuit();
    
    // 1. 儲存設定
    saveSettings();
    
    // 2. 關閉所有文件
    if (d->documentManager) {
        d->documentManager->closeAll();
    }
    
    // 3. 清理外掛
    if (d->pluginManager) {
        d->pluginManager->unloadAll();
    }
    
    // 4. 關閉 Lisp 引擎
    if (d->lispEngine) {
        // Lisp 引擎會在解構時自動關閉
    }
    
    d->initialized = false;
    
    qDebug() << "[Application] Shutdown complete";
}

bool Application::isInitialized() const {
    return d->initialized;
}

cad::DocumentManager* Application::documentManager() const {
    return d->documentManager;
}

commands::CommandManager* Application::commandManager() const {
    return d->commandManager;
}

view::ViewManager* Application::viewManager() const {
    return d->viewManager;
}

scripting::LispEngine* Application::lispEngine() const {
    return d->lispEngine;
}

EventBus* Application::eventBus() const {
    return d->eventBus;
}

PluginManager* Application::pluginManager() const {
    return d->pluginManager;
}

QVariant Application::setting(const QString& key, const QVariant& defaultValue) const {
    if (!d->settings) {
        return defaultValue;
    }
    return d->settings->value(key, defaultValue);
}

void Application::setSetting(const QString& key, const QVariant& value) {
    if (!d->settings) {
        return;
    }
    
    d->settings->setValue(key, value);
    Q_EMIT settingChanged(key, value);
}

void Application::saveSettings() {
    if (d->settings) {
        d->settings->sync();
        qDebug() << "[Application] Settings saved to" << d->settingsPath;
    }
}

QString Application::version() {
    return QString(AICAD_VERSION);
}

QString Application::buildDate() {
    return QString(AICAD_BUILD_DATE);
}

QString Application::buildInfo() {
    QString info;
    info += QString("Qt %1 (%2-bit)\n").arg(qVersion()).arg(QSysInfo::WordSize);
    info += QString("Compiler: %1\n").arg(
#if defined(Q_CC_MSVC)
        "MSVC"
#elif defined(Q_CC_GNU)
        "GCC"
#elif defined(Q_CC_CLANG)
        "Clang"
#else
        "Unknown"
#endif
    );
    info += QString("Build type: %1\n").arg(
#ifdef NDEBUG
        "Release"
#else
        "Debug"
#endif
    );
    info += QString("Platform: %1").arg(QSysInfo::prettyProductName());
    return info;
}

bool Application::initializeEventBus() {
    qDebug() << "[Application] Initializing EventBus";
    
    d->eventBus = new EventBus(this);
    
    if (!d->eventBus) {
        return false;
    }
    
    qDebug() << "[Application] EventBus initialized";
    return true;
}

bool Application::initializeDocumentManager() {
    qDebug() << "[Application] Initializing DocumentManager";
    
    d->documentManager = new cad::DocumentManager(this);
    
    if (!d->documentManager) {
        return false;
    }
    
    // 連接文件管理器信號到事件總線
    connect(d->documentManager, &cad::DocumentManager::documentCreated,
            this, [this](cad::Document* doc) {
        d->eventBus->publish("document.created", QVariant::fromValue(doc));
    });
    
    connect(d->documentManager, &cad::DocumentManager::documentClosed,
            this, [this](cad::Document* doc) {
        d->eventBus->publish("document.closed", QVariant::fromValue(doc));
    });
    
    qDebug() << "[Application] DocumentManager initialized";
    return true;
}

bool Application::initializeCommandManager() {
    qDebug() << "[Application] Initializing CommandManager";
    
    d->commandManager = new commands::CommandManager(this);
    
    if (!d->commandManager) {
        return false;
    }
    
    // 連接命令管理器信號到事件總線
    connect(d->commandManager, &commands::CommandManager::commandExecuted,
            this, [this](commands::Command* cmd) {
        d->eventBus->publish("command.executed", QVariant::fromValue(cmd));
    });
    
    qDebug() << "[Application] CommandManager initialized";
    return true;
}

bool Application::initializeViewManager() {
    qDebug() << "[Application] Initializing ViewManager";
    
    d->viewManager = new view::ViewManager(this);
    
    if (!d->viewManager) {
        return false;
    }
    
    // 連接視圖管理器信號到事件總線
    connect(d->viewManager, &view::ViewManager::viewCreated,
            this, [this](view::CadView* view) {
        d->eventBus->publish("view.created", QVariant::fromValue(view));
    });
    
    qDebug() << "[Application] ViewManager initialized";
    return true;
}

bool Application::initializeLispEngine() {
    qDebug() << "[Application] Initializing LispEngine";
    
    d->lispEngine = new scripting::LispEngine(this);
    
    if (!d->lispEngine) {
        return false;
    }
    
    if (!d->lispEngine->initialize()) {
        qWarning() << "[Application] LispEngine initialization failed";
        delete d->lispEngine;
        d->lispEngine = nullptr;
        return false;
    }
    
    // 連接 Lisp 輸出
    connect(d->lispEngine, &scripting::LispEngine::output,
            this, [](const QString& text) {
        qDebug() << "[Lisp]" << text;
    });
    
    connect(d->lispEngine, &scripting::LispEngine::errorOccurred,
            this, [this](const QString& error) {
        qWarning() << "[Lisp Error]" << error;
        Q_EMIT errorOccurred(error);
    });
    
    qDebug() << "[Application] LispEngine initialized";
    return true;
}

bool Application::initializePluginManager() {
    qDebug() << "[Application] Initializing PluginManager";
    
    d->pluginManager = new PluginManager(this);
    
    if (!d->pluginManager) {
        return false;
    }
    
    // 載入外掛目錄
    QString pluginDir = setting("plugins/directory", "plugins").toString();
    d->pluginManager->loadPluginsFromDirectory(pluginDir);
    
    qDebug() << "[Application] PluginManager initialized";
    return true;
}

void Application::loadSettings() {
    // 設定檔路徑 (跨平台)
    QString configPath = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    
    QDir().mkpath(configPath);
    
    d->settingsPath = configPath + "/aicad.ini";
    d->settings = new QSettings(d->settingsPath, QSettings::IniFormat, this);
    
    qDebug() << "[Application] Settings loaded from" << d->settingsPath;
    
    // 設定預設值
    if (!d->settings->contains("general/language")) {
        d->settings->setValue("general/language", "en");
    }
    
    if (!d->settings->contains("scripting/enableLisp")) {
        d->settings->setValue("scripting/enableLisp", true);
    }
    
    if (!d->settings->contains("view/backgroundColor")) {
        d->settings->setValue("view/backgroundColor", "#C8C8C8");
    }
}

void Application::connectSubsystems() {
    qDebug() << "[Application] Connecting subsystems";
    
    // 文件管理器 -> 視圖管理器
    if (d->documentManager && d->viewManager) {
        connect(d->documentManager, &cad::DocumentManager::activeDocumentChanged,
                d->viewManager, &view::ViewManager::setActiveDocument);
    }
    
    // 命令管理器 -> 文件管理器
    if (d->commandManager && d->documentManager) {
        // 命令可能需要存取當前文件
        // 透過事件總線或直接存取
    }
    
    // 其他子系統連接...
    
    qDebug() << "[Application] Subsystems connected";
}

} // namespace core
} // namespace aicad