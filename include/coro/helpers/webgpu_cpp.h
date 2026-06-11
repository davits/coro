#pragma once

#include <webgpu/webgpu_cpp.h>

#include "../core/task.hpp"
#include "../sync/latch.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace coro::wgpu {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline coro::Task<::wgpu::Adapter> RequestAdapter(::wgpu::Instance instance,
                                                  ::wgpu::RequestAdapterOptions const* options = nullptr) {
    Latch latch {1};
    ::wgpu::Adapter result;
    std::exception_ptr eptr = nullptr;
    instance.RequestAdapter(options,
                            ::wgpu::CallbackMode::AllowSpontaneous,
                            [&](::wgpu::RequestAdapterStatus status, ::wgpu::Adapter adapter, const char* message) {
                                switch (status) {
                                case ::wgpu::RequestAdapterStatus::Success:
                                    result = std::move(adapter);
                                    break;
                                case ::wgpu::RequestAdapterStatus::CallbackCancelled:
                                case ::wgpu::RequestAdapterStatus::Unavailable:
                                case ::wgpu::RequestAdapterStatus::Error:
                                    eptr = std::make_exception_ptr(Error {message});
                                    break;
                                }
                                latch.count_down();
                            });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return result;
}

inline coro::Task<::wgpu::Device> RequestDevice(::wgpu::Adapter adapter,
                                                ::wgpu::DeviceDescriptor const* descriptor = nullptr) {
    Latch latch {1};
    ::wgpu::Device result;
    std::exception_ptr eptr = nullptr;
    adapter.RequestDevice(descriptor,
                          ::wgpu::CallbackMode::AllowSpontaneous,
                          [&](::wgpu::RequestDeviceStatus status, ::wgpu::Device device, const char* message) {
                              switch (status) {
                              case ::wgpu::RequestDeviceStatus::Success:
                                  result = std::move(device);
                                  break;
                              case ::wgpu::RequestDeviceStatus::CallbackCancelled:
                              case ::wgpu::RequestDeviceStatus::Error:
                                  eptr = std::make_exception_ptr(Error {message});
                                  break;
                              }
                              latch.count_down();
                          });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return result;
}

inline coro::Task<void> MapAsync(::wgpu::Buffer buffer, ::wgpu::MapMode mode, std::size_t offset, std::size_t size) {
    Latch latch {1};
    std::exception_ptr eptr = nullptr;
    buffer.MapAsync(mode,
                    offset,
                    size,
                    ::wgpu::CallbackMode::AllowSpontaneous,
                    [&](::wgpu::MapAsyncStatus status, const char* message) {
                        switch (status) {
                        case ::wgpu::MapAsyncStatus::Success:
                            break;
                        case ::wgpu::MapAsyncStatus::CallbackCancelled:
                        case ::wgpu::MapAsyncStatus::Error:
                        case ::wgpu::MapAsyncStatus::Aborted:
                            eptr = std::make_exception_ptr(Error {message});
                            break;
                        }
                        latch.count_down();
                    });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

inline coro::Task<void> OnSubmittedWorkDone(::wgpu::Queue queue) {
    Latch latch {1};
    std::exception_ptr eptr = nullptr;
    queue.OnSubmittedWorkDone(::wgpu::CallbackMode::AllowSpontaneous,
                              [&](::wgpu::QueueWorkDoneStatus status, const char* message) {
                                  switch (status) {
                                  case ::wgpu::QueueWorkDoneStatus::Success:
                                      break;
                                  case ::wgpu::QueueWorkDoneStatus::CallbackCancelled:
                                  case ::wgpu::QueueWorkDoneStatus::Error:
                                      eptr = std::make_exception_ptr(Error {message});
                                      break;
                                  }
                                  latch.count_down();
                              });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

/// co_return is the ErrorType captured by the popped scope (NoError when the
/// scope was clean). A non-Success PopErrorScopeStatus means the pop itself
/// failed and is thrown as Error.
inline coro::Task<::wgpu::ErrorType> PopErrorScope(::wgpu::Device device) {
    Latch latch {1};
    ::wgpu::ErrorType result {};
    std::exception_ptr eptr = nullptr;
    device.PopErrorScope(::wgpu::CallbackMode::AllowSpontaneous,
                         [&](::wgpu::PopErrorScopeStatus status, ::wgpu::ErrorType type, const char* message) {
                             switch (status) {
                             case ::wgpu::PopErrorScopeStatus::Success:
                                 result = type;
                                 break;
                             case ::wgpu::PopErrorScopeStatus::CallbackCancelled:
                             case ::wgpu::PopErrorScopeStatus::Error:
                                 eptr = std::make_exception_ptr(Error {message});
                                 break;
                             }
                             latch.count_down();
                         });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return result;
}

/// Returns the shader's compilation diagnostics as {errors, warnings + info}.
/// Each message is formatted as "line:pos: text" on its own line; messages in
/// the second string are additionally prefixed with their severity. Both
/// strings are empty on a clean compile. The CompilationInfo Dawn delivers is
/// only valid inside the callback, so the text is copied out here.
inline coro::Task<std::pair<std::string, std::string>> GetCompilationInfo(::wgpu::ShaderModule shaderModule) {
    Latch latch {1};
    std::pair<std::string, std::string> result;
    std::exception_ptr eptr = nullptr;
    shaderModule.GetCompilationInfo(
        ::wgpu::CallbackMode::AllowSpontaneous,
        [&](::wgpu::CompilationInfoRequestStatus status, ::wgpu::CompilationInfo const* info) {
            switch (status) {
            case ::wgpu::CompilationInfoRequestStatus::Success:
                for (std::size_t i = 0; i < info->messageCount; ++i) {
                    const auto& message = info->messages[i];
                    const bool isError = message.type == ::wgpu::CompilationMessageType::Error;
                    std::string& out = isError ? result.first : result.second;
                    if (isError) {
                        out += "error ";
                    } else {
                        out += message.type == ::wgpu::CompilationMessageType::Warning ? "warning " : "info ";
                    }
                    out += std::to_string(message.lineNum);
                    out += ':';
                    out += std::to_string(message.linePos);
                    out += ": ";
                    out += std::string_view {message.message};
                    out += '\n';
                }
                break;
            case ::wgpu::CompilationInfoRequestStatus::CallbackCancelled:
                eptr = std::make_exception_ptr(Error {"compilation info request was cancelled"});
                break;
            }
            latch.count_down();
        });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return result;
}

inline coro::Task<::wgpu::ComputePipeline>
CreateComputePipelineAsync(::wgpu::Device device, ::wgpu::ComputePipelineDescriptor const* descriptor) {
    Latch latch {1};
    ::wgpu::ComputePipeline result;
    std::exception_ptr eptr = nullptr;
    device.CreateComputePipelineAsync(
        descriptor,
        ::wgpu::CallbackMode::AllowSpontaneous,
        [&](::wgpu::CreatePipelineAsyncStatus status, ::wgpu::ComputePipeline pipeline, const char* message) {
            switch (status) {
            case ::wgpu::CreatePipelineAsyncStatus::Success:
                result = std::move(pipeline);
                break;
            case ::wgpu::CreatePipelineAsyncStatus::CallbackCancelled:
            case ::wgpu::CreatePipelineAsyncStatus::ValidationError:
            case ::wgpu::CreatePipelineAsyncStatus::InternalError:
                eptr = std::make_exception_ptr(Error {message});
                break;
            }
            latch.count_down();
        });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return result;
}

inline coro::Task<::wgpu::RenderPipeline>
CreateRenderPipelineAsync(::wgpu::Device device, ::wgpu::RenderPipelineDescriptor const* descriptor) {
    Latch latch {1};
    ::wgpu::RenderPipeline result;
    std::exception_ptr eptr = nullptr;
    device.CreateRenderPipelineAsync(
        descriptor,
        ::wgpu::CallbackMode::AllowSpontaneous,
        [&](::wgpu::CreatePipelineAsyncStatus status, ::wgpu::RenderPipeline pipeline, const char* message) {
            switch (status) {
            case ::wgpu::CreatePipelineAsyncStatus::Success:
                result = std::move(pipeline);
                break;
            case ::wgpu::CreatePipelineAsyncStatus::CallbackCancelled:
            case ::wgpu::CreatePipelineAsyncStatus::ValidationError:
            case ::wgpu::CreatePipelineAsyncStatus::InternalError:
                eptr = std::make_exception_ptr(Error {message});
                break;
            }
            latch.count_down();
        });
    co_await latch;
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    co_return result;
}

} // namespace coro::wgpu
