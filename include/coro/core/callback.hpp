#pragma once

#include <functional>
#include <memory>

namespace coro {

class Callback {
public:
    using Ref = std::shared_ptr<Callback>;
    using WeakRef = std::weak_ptr<Callback>;
    using Func = std::function<void()>;

private:
    struct Tag {};

public:
    Callback(Tag, Func&& function)
        : _function(std::move(function)) {}

    static Ref create(Func function) {
        return std::make_shared<Callback>(Tag {}, std::move(function));
    }

    void invoke() noexcept {
        if (_function) {
            try {
                _function();
            } catch (...) {
            }
            _function = Func {};
        }
    }

private:
    Func _function;
};

} // namespace coro
