/**
 * @file UIManager.cpp
 * @brief UIManager 類別實作
 * @author James
 * @date 2025-01-07
 */

#include "UIManager.h"
#include "MainWindow.h"
#include "ToolManager.h"
#include "FeatureBrowser.h"
#include "PropertyPanel.h"
#include "view/ViewManager.h"  // ✅ 添加
#include "view/CadView.h"      // ✅ 添加
#include "core/Application.h"
#include "core/EventBus.h"
#include "core/DocumentManager.h"

#include <QDebug>

namespace aicad {
namespace ui {

class UIManager::Private {
public:
    Private()
        : mainWindow(nullptr)
        , featureBrowser(nullptr)
        , propertyPanel(nullptr)
        , toolManager(nullptr)
        , cadView(nullptr)           // ✅ 添加
        , initialized(false)
    {
    }
    
    ~Private() {
        // MainWindow 會自動刪除子元件
        delete mainWindow;
    }
    
    MainWindow* mainWindow;
    FeatureBrowser* featureBrowser;
    PropertyPanel* propertyPanel;
    ToolManager* toolManager;
    view::CadView* cadView;          // ✅ 添加
    bool initialized;
};

UIManager::UIManager(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[UIManager] Created";
}

UIManager::~UIManager() {
    qDebug() << "[UIManager] Destroyed";
    delete d;
}

bool UIManager::initialize() {
    if (d->initialized) {
        qWarning() << "[UIManager] Already initialized";
        return true;
    }
    
    qDebug() << "[UIManager] Initializing...";
    
    try {
        // 取得核心系統
        core::Application* app = core::Application::instance();
        core::EventBus* bus = app->eventBus();
        core::DocumentManager* docMgr = app->documentManager();
        
        if (!bus || !docMgr) {
            qCritical() << "[UIManager] Core systems not available";
            return false;
        }
        
        // 1. 建立主視窗
        qDebug() << "[UIManager] Creating MainWindow...";
        d->mainWindow = new MainWindow();
        
        // 2. 建立特徵瀏覽器
        qDebug() << "[UIManager] Creating FeatureBrowser...";
        d->featureBrowser = new FeatureBrowser(d->mainWindow);
        d->mainWindow->addDockWidget(Qt::LeftDockWidgetArea, d->featureBrowser);
        
        // 3. 建立屬性面板
        qDebug() << "[UIManager] Creating PropertyPanel...";
        d->propertyPanel = new PropertyPanel(d->mainWindow);
        d->mainWindow->addDockWidget(Qt::RightDockWidgetArea, d->propertyPanel);
        
        // 4. 建立工具管理器
        qDebug() << "[UIManager] Creating ToolManager...";
        d->toolManager = new ToolManager(d->mainWindow);

        // 5. 建立 CAD 視圖並設為中央 Widget  // ✅ 添加
        qDebug() << "[UIManager] Creating CadView...";
        d->cadView = new view::CadView(d->mainWindow);
        d->mainWindow->setCentralWidget(d->cadView);

        // 6. 連接事件總線
        qDebug() << "[UIManager] Connecting to EventBus...";
        
        // 監聽文件事件
        bus->subscribe(core::Events::DOCUMENT_CREATED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Document created:" << data.toString();
                updateFeatureTree();
            });
        
        bus->subscribe(core::Events::DOCUMENT_CLOSED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Document closed:" << data.toString();
                updateFeatureTree();
            });
        
        // 監聽特徵事件
        bus->subscribe(core::Events::FEATURE_CREATED, this,
            [this](const QVariant& data) {
                qDebug() << "[UIManager] Feature created:" << data.toString();
                updateFeatureTree();
            });
        
        // 6. 連接 DocumentManager 信號
        connect(docMgr, &core::DocumentManager::documentCreated,
                this, [this](const QString& name) {
            qDebug() << "[UIManager] DocumentManager created document:" << name;
            setStatusMessage(QString("Document created: %1").arg(name), 3000);
        });
        
        connect(docMgr, &core::DocumentManager::currentDocumentChanged,
                d->featureBrowser, &FeatureBrowser::setCurrentDocument);
        
        // 7. 連接主視窗關閉信號
        connect(d->mainWindow, &MainWindow::aboutToClose,
                this, &UIManager::mainWindowClosed);
        
        d->initialized = true;
        qDebug() << "[UIManager] Initialization completed";
        
        Q_EMIT initialized();
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "[UIManager] Initialization failed:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "[UIManager] Initialization failed: Unknown exception";
        return false;
    }
}

void UIManager::showMainWindow() {
    if (!d->mainWindow) {
        qWarning() << "[UIManager] MainWindow not created";
        return;
    }
    
    qDebug() << "[UIManager] Showing MainWindow";
    d->mainWindow->show();
}

MainWindow* UIManager::mainWindow() const {
    return d->mainWindow;
}

FeatureBrowser* UIManager::featureBrowser() const {
    return d->featureBrowser;
}

PropertyPanel* UIManager::propertyPanel() const {
    return d->propertyPanel;
}

ToolManager* UIManager::toolManager() const {
    return d->toolManager;
}

void UIManager::updateFeatureTree() {
    if (!d->featureBrowser) {
        return;
    }
    
    qDebug() << "[UIManager] Updating feature tree";
    d->featureBrowser->refresh();
}

void UIManager::setStatusMessage(const QString& message, int timeout) {
    if (!d->mainWindow) {
        return;
    }
    
    d->mainWindow->statusBar()->showMessage(message, timeout);
}

// 添加 getter
view::CadView* UIManager::cadView() const {
    return d->cadView;
}

} // namespace ui
} // namespace aicad
