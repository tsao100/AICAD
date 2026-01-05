#include "LispBindings.h"

#include "core/Application.h"
#include "command/CommandManager.h"

using namespace aicad::core;

namespace aicad::scripting {

void LispBindings::registerAll(LispEngine* engine)
{
    // ★ 核心：通用 command dispatcher
    engine->registerFunction("*", [](const LispEngine::Args& args) -> QVariant {
        // 這個不會被直接呼叫
        return {};
    });

    // 攔截所有未知 function
    engine->setFallbackHandler(
        [](const QString& fn, const LispEngine::Args& args) -> QVariant {

            auto* cmdMgr = aicadApp->commandManager();

            if (!cmdMgr->hasCommand(fn)) {
                qWarning() << "[Lisp] Unknown command:" << fn;
                return {};
            }

            cmdMgr->execute(fn, args);
            return {};
        }
        );
}

}
