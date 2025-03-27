#include "bridge.hpp"

#include <emscripten/em_macros.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE void _coro_lib_awaiter_resolve(coro::ValAwaitable* awaiter, emscripten::EM_VAL result) {
    awaiter->resolve(emscripten::val::take_ownership(result));
}

EMSCRIPTEN_KEEPALIVE void _coro_lib_awaiter_reject(coro::ValAwaitable* awaiter, emscripten::EM_VAL error) {
    awaiter->reject(emscripten::val::take_ownership(error));
}
}
