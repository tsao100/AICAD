#!/bin/bash
# setup_project_structure.sh
# 為 AICAD 專案建立目錄結構和空檔案

set -e  # 遇到錯誤立即停止

echo "=========================================="
echo "  AICAD Project Structure Setup"
echo "=========================================="

# 基礎目錄
BASE_DIR="src"

# 建立主目錄結構
echo "Creating directory structure..."

mkdir -p "$BASE_DIR/core"
mkdir -p "$BASE_DIR/cad"
mkdir -p "$BASE_DIR/ui"
mkdir -p "$BASE_DIR/view"
mkdir -p "$BASE_DIR/command"
mkdir -p "$BASE_DIR/scripting"

echo "✓ Directories created"

# 定義檔案陣列 (檔案名稱)
# Core 模組
CORE_FILES=(
    "Application"
    "EventBus"
    "DocumentManager"
    "PluginManager"
    "Settings"
)

# CAD 模組 (Ben 負責)
CAD_FILES=(
    "Document"
    "Feature"
    "Sketch"
    "SketchEntity"
    "Extrude"
    "FeatureTree"
)

# UI 模組 (James 負責)
UI_FILES=(
    "MainWindow"
    "FeatureBrowser"
    "PropertyPanel"
    "ToolbarManager"
)

# View 模組 (Felicia 負責)
VIEW_FILES=(
    "ViewManager"
    "CadView"
    "RubberBand"
    "SelectionManager"
)

# Command 模組 (Kaufen 負責)
COMMAND_FILES=(
    "CommandManager"
    "Command"
    "RectangleCommand"
    "LineCommand"
    "CircleCommand"
)

# Scripting 模組 (Daney 負責)
SCRIPTING_FILES=(
    "LispEngine"
    "LispBindings"
    "ScriptManager"
)

# 建立檔案的函數
create_files() {
    local dir=$1
    shift
    local files=("$@")
    
    for file in "${files[@]}"; do
        local header="$BASE_DIR/$dir/$file.h"
        local source="$BASE_DIR/$dir/$file.cpp"
        
        # 建立 .h 檔案
        if [ ! -f "$header" ]; then
            cat > "$header" << EOF
/**
 * @file $file.h
 * @brief $file 類別定義
 * @author TODO
 * @date $(date +%Y-%m-%d)
 */

#ifndef AICAD_${dir^^}_${file^^}_H
#define AICAD_${dir^^}_${file^^}_H

#include <QObject>

namespace aicad {
namespace $dir {

/**
 * @brief $file 類別
 * 
 * TODO: 添加類別說明
 */
class $file : public QObject {
    Q_OBJECT
    
public:
    explicit $file(QObject* parent = nullptr);
    ~$file() override;
    
    // TODO: 添加公開方法
    
Q_SIGNALS:
    // TODO: 添加信號
    
private:
    // TODO: 添加私有成員
    
    class Private;
    Private* d;
};

} // namespace $dir
} // namespace aicad

#endif // AICAD_${dir^^}_${file^^}_H
EOF
            echo "  Created: $header"
        else
            echo "  Skipped: $header (already exists)"
        fi
        
        # 建立 .cpp 檔案
        if [ ! -f "$source" ]; then
            cat > "$source" << EOF
/**
 * @file $file.cpp
 * @brief $file 類別實作
 * @author TODO
 * @date $(date +%Y-%m-%d)
 */

#include "$file.h"
#include <QDebug>

namespace aicad {
namespace $dir {

class ${file}::Private {
public:
    Private() {
        // TODO: 初始化成員
    }
    
    ~Private() {
        // TODO: 清理資源
    }
    
    // TODO: 添加私有成員變數
};

${file}::${file}(QObject* parent)
    : QObject(parent)
    , d(new Private())
{
    qDebug() << "[$file] Created";
    // TODO: 實作建構子
}

${file}::~${file}() {
    qDebug() << "[$file] Destroyed";
    delete d;
}

// TODO: 實作其他方法

} // namespace $dir
} // namespace aicad
EOF
            echo "  Created: $source"
        else
            echo "  Skipped: $source (already exists)"
        fi
    done
}

# 為現有檔案建立備份
echo ""
echo "Backing up existing files..."
if [ -d "$BASE_DIR" ]; then
    BACKUP_DIR="backup_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$BACKUP_DIR"
    
    # 備份現有的 main.cpp 和其他核心檔案
    for file in main.cpp MainWindow.h MainWindow.cpp CadView.h CadView.cpp OcafDocument.h OcafDocument.cpp; do
        if [ -f "$BASE_DIR/$file" ]; then
            cp "$BASE_DIR/$file" "$BACKUP_DIR/"
            echo "  Backed up: $file"
        fi
    done
    
    echo "✓ Backup completed in $BACKUP_DIR/"
else
    echo "  No existing files to backup"
fi

# 建立各模組的檔案
echo ""
echo "Creating Core module files..."
create_files "core" "${CORE_FILES[@]}"

echo ""
echo "Creating CAD module files (Ben)..."
create_files "cad" "${CAD_FILES[@]}"

echo ""
echo "Creating UI module files (James)..."
create_files "ui" "${UI_FILES[@]}"

echo ""
echo "Creating View module files (Felicia)..."
create_files "view" "${VIEW_FILES[@]}"

echo ""
echo "Creating Command module files (Kaufen)..."
create_files "command" "${COMMAND_FILES[@]}"

echo ""
echo "Creating Scripting module files (Daney)..."
create_files "scripting" "${SCRIPTING_FILES[@]}"

# 建立 main.cpp (如果不存在)
if [ ! -f "$BASE_DIR/main.cpp" ]; then
    cat > "$BASE_DIR/main.cpp" << 'EOF'
/**
 * @file main.cpp
 * @brief AICAD 應用程式入口點
 * @date 2024-12-04
 */

#include <QApplication>
#include <QSurfaceFormat>
#include <QDebug>
#include <QMessageBox>

#include "ui/MainWindow.h"
#include "core/Application.h"

int main(int argc, char **argv) {
#ifdef _WIN32
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(fmt);
#else
    // Force Qt to use XCB on Linux
    const char* session = std::getenv("XDG_SESSION_TYPE");
    if (session && std::strcmp(session, "wayland") == 0) {
        qDebug("Detected Wayland session → forcing xcb");
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif

    QApplication qapp(argc, argv);
    
    qapp.setOrganizationName("AICAD Team");
    qapp.setApplicationName("AICAD");
    qapp.setApplicationVersion("1.0.0-dev");
    
    qDebug() << "===========================================";
    qDebug() << "  AICAD - Advanced Interactive CAD System";
    qDebug() << "  Version:" << qapp.applicationVersion();
    qDebug() << "===========================================";
    
    // 初始化 AICAD 核心系統
    aicad::core::Application* app = aicad::core::Application::instance();
    
    if (!app->initialize()) {
        QMessageBox::critical(nullptr, "Initialization Failed",
                            "Failed to initialize AICAD core system.\n"
                            "Please check the console for error messages.");
        return 1;
    }
    
    qDebug() << "AICAD Core initialized successfully";
    
    // 建立主視窗
    aicad::ui::MainWindow mainWindow;
    mainWindow.show();
    
    qDebug() << "Main window shown";
    qDebug() << "Entering event loop...";
    
    int result = qapp.exec();
    
    qDebug() << "Event loop exited with code:" << result;
    app->shutdown();
    
    return result;
}
EOF
    echo ""
    echo "Created: $BASE_DIR/main.cpp"
fi

echo ""
echo "=========================================="
echo "✓ Project structure setup completed!"
echo "=========================================="
echo ""
echo "Directory structure:"
tree -L 2 src/ 2>/dev/null || find src/ -type d | sed 's|[^/]*/| |g'
echo ""
echo "Next steps:"
echo "1. Review the generated files"
echo "2. Update AICAD.pro with the new source files"
echo "3. Run: qmake && make"
echo ""
echo "Team assignments:"
echo "  - Ben:     src/cad/     (CAD Engine)"
echo "  - James:   src/ui/      (UI System)"
echo "  - Felicia: src/view/    (View System)"
echo "  - Kaufen:  src/command/ (Commands)"
echo "  - Daney:   src/scripting/ (Scripting)"
echo ""