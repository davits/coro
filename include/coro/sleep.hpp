#pragma once

#ifdef CORO_EMSCRIPTEN
#include "emscripten/sleep.hpp"
#else
#include "detail/sleep.hpp"
#endif
