/**
 * @file ComponentDialogs.h
 * @brief 3D 組件安裝用的兩個對話框：
 *        - ComponentRoleDialog：指定來源 *.aicad 內每個實體的角色（組件／所需空間／略過）
 *        - ComponentPlacementDialog：編輯安裝位置（線路、里程、橫向偏移、旋轉 180°）
 */
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cad/ComponentInstall.h"

class QTableWidget;

namespace aicad {
namespace cad { class Document; }
namespace ui {

// ─────────────────────────────────────────────────────────────────────────────
class ComponentRoleDialog : public QDialog {
    Q_OBJECT
public:
    enum Role { RoleComponent = 0, RoleSpace = 1, RoleIgnore = 2 };

    struct Candidate {
        QString name;
        QString typeName;
    };

    ComponentRoleDialog(const QVector<Candidate>& candidates, QWidget* parent = nullptr);

    /// 與 candidates 同順序的角色
    QVector<Role> roles() const;

private:
    QTableWidget* m_table = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
class ComponentPlacementDialog : public QDialog {
    Q_OBJECT
public:
    ComponentPlacementDialog(cad::Document* doc,
                             const QVector<cad::ComponentInstall::Placement>& initial,
                             QWidget* parent = nullptr);

    QVector<cad::ComponentInstall::Placement> placements() const { return m_result; }

protected:
    void accept() override;

private:
    void addRow(const cad::ComponentInstall::Placement& p);

    cad::Document* m_doc = nullptr;
    QTableWidget*  m_table = nullptr;
    QVector<cad::ComponentInstall::Placement> m_result;
};

// ─────────────────────────────────────────────────────────────────────────────
/// 各式組件沿線形的安裝位置：一張表，每列選擇「哪個組件」＋ 線路／里程／橫向偏移／旋轉 180°
class ComponentLayoutDialog : public QDialog {
    Q_OBJECT
public:
    explicit ComponentLayoutDialog(cad::Document* doc, QWidget* parent = nullptr);

    /// 組件 id → 該組件的安裝位置（沒有任何列的組件也會出現，值為空）
    QHash<QString, QVector<cad::ComponentInstall::Placement>> result() const { return m_result; }

protected:
    void accept() override;

private:
    void addRow(const QString& componentId, const cad::ComponentInstall::Placement& p);

    cad::Document* m_doc = nullptr;
    QTableWidget*  m_table = nullptr;
    QHash<QString, QVector<cad::ComponentInstall::Placement>> m_result;
};

} // namespace ui
} // namespace aicad
