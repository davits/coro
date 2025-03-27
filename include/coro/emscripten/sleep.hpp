#pragma once

#include "bridge.hpp"

#include <emscripten/val.h>

namespace coro {

namespace detail {
extern "C" emscripten::EM_VAL _coro_lib_sleep(int32_t ms);
}

inline emscripten::val sleep(uint32_t ms) {
    return emscripten::val::take_ownership(detail::_coro_lib_sleep(static_cast<int32_t>(ms)));
}

} // namespace coro
