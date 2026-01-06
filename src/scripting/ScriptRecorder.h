// src/scripting/ScriptRecorder.h

#pragma once
#include <QString>
#include <QStringList>

namespace aicad::scripting {

class ScriptRecorder {
public:
    static ScriptRecorder& instance();

    void start();
    void stop();
    bool isRecording() const;

    void recordCommand(const QString& commandId,
                       const QVariantList& args);

    QString script() const;
    void clear();

private:
    ScriptRecorder() = default;

    bool m_recording = false;
    QStringList m_lines;
};

}
