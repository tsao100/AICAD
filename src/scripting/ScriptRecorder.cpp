#include "ScriptRecorder.h"
#include "MenuCommandLoader.h"

namespace aicad::scripting {

ScriptRecorder& ScriptRecorder::instance()
{
    static ScriptRecorder inst;
    return inst;
}

void ScriptRecorder::start()
{
    m_recording = true;
    m_lines.clear();
    m_lines << ";; --- Script Recorder Start ---";
}

void ScriptRecorder::stop()
{
    m_lines << ";; --- Script Recorder End ---";
    m_recording = false;
}

bool ScriptRecorder::isRecording() const
{
    return m_recording;
}

void ScriptRecorder::recordCommand(const QString& id,
                                   const QVariantList& args)
{
    if (!m_recording)
        return;

    const auto& meta = MenuCommandLoader::metadata().value(id);
    QString line = meta.scriptTemplate;

    // $1 $2 → 實際值
    for (int i = 0; i < args.size(); ++i) {
        line.replace(QString("$%1").arg(i + 1),
                     args[i].toString());
    }

    m_lines << line;
}

QString ScriptRecorder::script() const
{
    return m_lines.join("\n");
}

void ScriptRecorder::clear()
{
    m_lines.clear();
}

}
