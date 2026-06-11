#include <coro/coro.hpp>
#include <coro/sleep.hpp>
#include <coro/executors/serial_executor.hpp>
#include <coro/helpers/webgpu_cpp.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace {

coro::Task<void> pumpEvents(wgpu::Instance instance, bool& done) {
    while (!done) {
        instance.ProcessEvents();
        co_await coro::sleep(10);
    }
}

coro::Task<void> webgpuFlow() {
    wgpu::Instance instance = wgpu::CreateInstance();
    auto executor = co_await coro::currentExecutor;

    bool done = false;
    executor->schedule(pumpEvents(instance, done));

    EXPECT_TRUE(instance);
    if (!instance) co_return;

    wgpu::Adapter adapter = co_await coro::wgpu::RequestAdapter(instance);
    EXPECT_TRUE(adapter);
    if (!adapter) co_return;

    wgpu::AdapterInfo info {};
    adapter.GetInfo(&info);
    EXPECT_NE(info.backendType, wgpu::BackendType::Null);
    if (info.backendType == wgpu::BackendType::Null) co_return;

    wgpu::Device device = co_await coro::wgpu::RequestDevice(adapter);
    EXPECT_TRUE(device);
    if (!device) co_return;

    wgpu::Queue queue = device.GetQueue();
    EXPECT_TRUE(queue);
    if (!queue) co_return;

    // Submit an empty batch and await its completion.
    queue.Submit(0, nullptr);
    co_await coro::wgpu::OnSubmittedWorkDone(queue);

    // Round-trip a value through a host-visible buffer:
    //   write -> queue -> map -> read.
    constexpr std::uint32_t kPayload = 0xC0FFEE42;

    wgpu::BufferDescriptor srcDesc {};
    srcDesc.size = sizeof(std::uint32_t);
    srcDesc.usage = wgpu::BufferUsage::CopySrc;
    srcDesc.mappedAtCreation = true;
    wgpu::Buffer srcBuffer = device.CreateBuffer(&srcDesc);
    EXPECT_TRUE(srcBuffer);
    if (!srcBuffer) co_return;
    *static_cast<std::uint32_t*>(srcBuffer.GetMappedRange()) = kPayload;
    srcBuffer.Unmap();

    wgpu::BufferDescriptor dstDesc {};
    dstDesc.size = sizeof(std::uint32_t);
    dstDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer dstBuffer = device.CreateBuffer(&dstDesc);
    EXPECT_TRUE(dstBuffer);
    if (!dstBuffer) co_return;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(srcBuffer, 0, dstBuffer, 0, sizeof(std::uint32_t));
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);

    co_await coro::wgpu::MapAsync(dstBuffer, wgpu::MapMode::Read, 0, sizeof(std::uint32_t));

    std::uint32_t roundTripped =
        *static_cast<const std::uint32_t*>(dstBuffer.GetConstMappedRange(0, sizeof(std::uint32_t)));
    EXPECT_EQ(roundTripped, kPayload);
    dstBuffer.Unmap();

    done = true;
}

} // namespace

TEST(WebGpu, AsyncWrappers) {
    auto executor = coro::SerialExecutor::create();
    EXPECT_NO_THROW(executor->syncWait(webgpuFlow()));
}
