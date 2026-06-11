#include <coro/coro.hpp>
#include <coro/emscripten/executor.hpp>
#include <coro/helpers/webgpu_cpp.h>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr char kComputeShader[] = R"(
@group(0) @binding(0) var<storage, read_write> data: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x < arrayLength(&data)) {
        data[gid.x] = data[gid.x] * 2u + 1u;
    }
}
)";

coro::Task<::wgpu::Device> requestDevice() {
    ::wgpu::Instance instance = ::wgpu::CreateInstance();
    if (!instance) {
        throw coro::wgpu::Error {"wgpu::CreateInstance() failed"};
    }
    ::wgpu::Adapter adapter = co_await coro::wgpu::RequestAdapter(instance);
    co_return co_await coro::wgpu::RequestDevice(adapter);
}

coro::Task<emscripten::val> testRequestAdapterAndDevice() {
    ::wgpu::Device device = co_await requestDevice();
    emscripten::val out = emscripten::val::object();
    out.set("haveDevice", static_cast<bool>(device));
    co_return out;
}

/**
 * End-to-end compute dispatch driving every awaitable wrapper on the happy
 * path: RequestAdapter → RequestDevice → GetCompilationInfo →
 * CreateComputePipelineAsync → OnSubmittedWorkDone → MapAsync. The shader
 * maps each u32 to `2x + 1`; the mapped-back results are returned as a JS
 * array so the test can verify the GPU actually ran the work.
 */
coro::Task<emscripten::val> testComputeRoundTrip(uint32_t count) {
    ::wgpu::Device device = co_await requestDevice();
    ::wgpu::Queue queue = device.GetQueue();

    ::wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = kComputeShader;
    ::wgpu::ShaderModuleDescriptor moduleDesc {.nextInChain = &wgsl};
    ::wgpu::ShaderModule module = device.CreateShaderModule(&moduleDesc);
    auto [errors, warnings] = co_await coro::wgpu::GetCompilationInfo(module);
    if (!errors.empty()) {
        throw coro::wgpu::Error {errors};
    }

    ::wgpu::ComputePipelineDescriptor pipelineDesc;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = "main";
    ::wgpu::ComputePipeline pipeline = co_await coro::wgpu::CreateComputePipelineAsync(device, &pipelineDesc);

    const uint64_t byteSize = uint64_t {count} * sizeof(uint32_t);
    ::wgpu::BufferDescriptor storageDesc;
    storageDesc.usage = ::wgpu::BufferUsage::Storage | ::wgpu::BufferUsage::CopySrc | ::wgpu::BufferUsage::CopyDst;
    storageDesc.size = byteSize;
    ::wgpu::Buffer storage = device.CreateBuffer(&storageDesc);

    ::wgpu::BufferDescriptor readbackDesc;
    readbackDesc.usage = ::wgpu::BufferUsage::MapRead | ::wgpu::BufferUsage::CopyDst;
    readbackDesc.size = byteSize;
    ::wgpu::Buffer readback = device.CreateBuffer(&readbackDesc);

    std::vector<uint32_t> input(count);
    for (uint32_t i = 0; i < count; ++i) {
        input[i] = i;
    }
    queue.WriteBuffer(storage, 0, input.data(), byteSize);

    ::wgpu::BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = storage;
    ::wgpu::BindGroupDescriptor bindGroupDesc;
    bindGroupDesc.layout = pipeline.GetBindGroupLayout(0);
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    ::wgpu::BindGroup bindGroup = device.CreateBindGroup(&bindGroupDesc);

    ::wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    ::wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, bindGroup);
    pass.DispatchWorkgroups((count + 63) / 64);
    pass.End();
    encoder.CopyBufferToBuffer(storage, 0, readback, 0, byteSize);
    ::wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);

    co_await coro::wgpu::OnSubmittedWorkDone(queue);
    co_await coro::wgpu::MapAsync(readback, ::wgpu::MapMode::Read, 0, byteSize);
    const auto* mapped = static_cast<const uint32_t*>(readback.GetConstMappedRange(0, byteSize));
    std::vector<uint32_t> output(mapped, mapped + count);
    readback.Unmap();
    co_return emscripten::val::array(output);
}

/**
 * Failure path: compiles a deliberately broken shader inside a validation
 * error scope and reports what GetCompilationInfo and PopErrorScope saw.
 */
coro::Task<emscripten::val> testShaderDiagnostics() {
    ::wgpu::Device device = co_await requestDevice();

    device.PushErrorScope(::wgpu::ErrorFilter::Validation);
    ::wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = "fn broken( { this is not wgsl";
    ::wgpu::ShaderModuleDescriptor moduleDesc {.nextInChain = &wgsl};
    ::wgpu::ShaderModule module = device.CreateShaderModule(&moduleDesc);
    auto [errors, warnings] = co_await coro::wgpu::GetCompilationInfo(module);
    ::wgpu::ErrorType scopeError = co_await coro::wgpu::PopErrorScope(device);

    emscripten::val out = emscripten::val::object();
    out.set("errors", errors);
    out.set("warningsAndInfo", warnings);
    out.set("scopeCaughtValidationError", scopeError == ::wgpu::ErrorType::Validation);
    co_return out;
}

} // namespace

EMSCRIPTEN_BINDINGS(CoroWebGpuTests) {
    emscripten::function("testRequestAdapterAndDevice",
                         +[]() { return coro::taskPromise(testRequestAdapterAndDevice()); });
    emscripten::function("testComputeRoundTrip",
                         +[](uint32_t count) { return coro::taskPromise(testComputeRoundTrip(count)); });
    emscripten::function("testShaderDiagnostics",
                         +[]() { return coro::taskPromise(testShaderDiagnostics()); });
}
