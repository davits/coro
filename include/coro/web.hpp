#pragma once

#include <emscripten/em_js.h>
#include <emscripten/val.h>

// clang-format off
EM_JS(emscripten::EM_VAL, _coroSleep, (int time), {
    return EmVal.toHandle(
        new Promise(resolve => { sleep(() => {resolve(false)}, time); })
    );
});
// clang-format on

namespace coro::web {

emscripten::val sleep(int32_t time) {
    return emscripten::val::take_ownership(_coroSleep(time));
}

} // namespace coro::web
