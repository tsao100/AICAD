#pragma once
#include "LispEngine.h"

namespace aicad::scripting {

class LispBindings {
public:
    static void registerAll(LispEngine* engine);
};

}
