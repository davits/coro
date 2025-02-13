#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace coro::detail {

template <typename T>
class Stack final {
public:
    struct Node {
        Node* next = nullptr;
        T data;

        Node(T d)
            : data(std::move(d)) {}
    };

public:
    void push(T data) {
        Node* node = new Node {std::move(data)};
        node->next = _head.load(std::memory_order_relaxed);
        while (!_head.compare_exchange_weak(node->next, node, std::memory_order_release, std::memory_order_relaxed))
            ;
    }

    std::unique_ptr<Node> pop() {
        Node* popped;
        do {
            popped = _head.load(std::memory_order_relaxed);
            if (popped == nullptr) {
                return nullptr;
            }
        } while (
            !_head.compare_exchange_weak(popped, popped->next, std::memory_order_release, std::memory_order_relaxed));

        popped->next = nullptr;
        return std::unique_ptr<Node> {popped};
    }

private:
    std::atomic<Node*> _head = nullptr;
};

} // namespace coro::detail
