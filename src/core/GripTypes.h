// core/GripTypes.h
#ifndef AICAD_CORE_GRIPTYPES_H
#define AICAD_CORE_GRIPTYPES_H

#include <QVector2D>
#include <QString>
#include <QVariant>

namespace aicad {
namespace core {

enum class GripType {
    StartPoint,    // 起點
    EndPoint,      // 終點
    MidPoint,      // 中點
    Center,        // 中心點
    Corner         // 角點
};

struct Grip {
    QString entityId;      // 關聯的實體 ID
    GripType type;         // Grip 類型
    QVector2D position;    // 位置
    int index;             // 索引（用於多個同類型 grip）
    bool isActive;         // 是否為活動狀態

    QVariantMap toVariant() const {
        QVariantMap map;
        map["entityId"] = entityId;
        map["type"] = static_cast<int>(type);
        map["position"] = QVariant::fromValue(position);
        map["index"] = index;
        map["isActive"] = isActive;
        return map;
    }

    static Grip fromVariant(const QVariantMap& map) {
        Grip grip;
        grip.entityId = map["entityId"].toString();
        grip.type = static_cast<GripType>(map["type"].toInt());
        grip.position = map["position"].value<QVector2D>();
        grip.index = map["index"].toInt();
        grip.isActive = map["isActive"].toBool();
        return grip;
    }
};

} // namespace core
} // namespace aicad

Q_DECLARE_METATYPE(aicad::core::Grip)
Q_DECLARE_METATYPE(aicad::core::GripType)

#endif
