#pragma once

#include <exception>
#include <functional>
#include <list>

#include <emscripten/val.h>

extern "C" emscripten::EM_VAL _coro_lib_val_from_cpp_exception();

namespace coro {

using ExceptionTranslator = std::function<emscripten::val(std::exception_ptr)>;

namespace detail {

inline std::list<ExceptionTranslator>& translators() {
    static std::list<ExceptionTranslator> instance;
    return instance;
}

inline emscripten::val defaultTranslator(std::exception_ptr eptr) {
    try {
        std::rethrow_exception(eptr);
    } catch (...) {
        return emscripten::val::take_ownership(_coro_lib_val_from_cpp_exception());
    }
}

} // namespace detail

inline void registerExceptionTranslator(ExceptionTranslator translator) {
    detail::translators().push_back(std::move(translator));
}

inline emscripten::val translateException(std::exception_ptr eptr) {
    auto& translators = detail::translators();
    for (auto& translator : translators) {
        emscripten::val error;
        try {
            // In case if one of the translators is poorly written and rethrows we consider it as not translated
            error = translator(eptr);
        } catch (...) {
        }
        if (!error.isUndefined()) {
            return error;
        }
    }
    return detail::defaultTranslator(eptr);
}

} // namespace coro
