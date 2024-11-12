#pragma once

namespace coro {

class SerialExecutor;

class TaskAllocator {
public:
    virtual void* alloc(size_t size) {
        void* ptr = alloc(size + sizeof(TaskAllocator*));
        auto alloc_ptr = reinterpret_cast<TaskAllocator**>(ptr);
        *alloc_ptr = this;
        return reinterpret_cast<void*>(reinterpret_cast<int8_t*>(ptr) + sizeof(TaskAllocator*));
    }

    virtual void free(void* ptr, size_t size) {
        ::free(ptr);
    }

    static void s_free(void* ptr, size_t size) {
        auto bytePtr = reinterpret_cast<int8_t*>(ptr) - sizeof(TaskAllocator*);
        auto allocator = *reinterpret_cast<TaskAllocator**>(bytePtr);
        allocator->free(ptr, size);
    }
};

class StackedTaskAllocator {
public:
    StackedTaskAllocator(size_t poolSize)
        : _pool(new char[poolSize])
        , _ptr(_pool) {}

    void* allocate(size_t size) {
        *reinterpret_cast<StackedTaskAllocator**>(_ptr) = this;
        _ptr += sizeof(StackedTaskAllocator*);
        auto ptr = _ptr;
        _ptr += size;
        return ptr;
    }

    void free(void*, size_t size) {
        _ptr -= (size + sizeof(StackedTaskAllocator*));
    }

    static void s_free(void* ptr, size_t size) {
        auto bytePtr = reinterpret_cast<char*>(ptr) - sizeof(StackedTaskAllocator*);
        auto alloc = *reinterpret_cast<StackedTaskAllocator**>(bytePtr);
        alloc->free(ptr, size);
    }

private:
    char* _pool;
    char* _ptr;
};
} // namespace coro