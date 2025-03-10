cmake_minimum_required(VERSION 3.21)

project(coro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CORO_TESTS "Enable tests." OFF)
option(CORO_SANITIZE_ASAN_AND_UBSAN "Enable ASan and UBSan." OFF)
option(CORO_SANITIZE_TSAN "Enable TSan." OFF)
option(CORO_ABORT_ON_SUSPENDED_TASK_DESTRUCTION "Abort when Task destructor is called for an unfinished/suspended task." OFF)

add_library(coro INTERFACE)
target_include_directories(coro INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CORO_TESTS OR CORO_ABORT_ON_SUSPENDED_TASK_DESTRUCTION)
    target_compile_definitions(coro INTERFACE CORO_ABORT_ON_SUSPENDED_TASK_DESTRUCTION)
endif()

if(CORO_TESTS)
    target_compile_options(coro INTERFACE -Wall -Wextra -Wnewline-eof -Wformat -Werror)

    set(SANITIZERS "")

    if(CORO_SANITIZE_TSAN AND CORO_SANITIZE_ASAN_AND_UBSAN)
        message(FATAL_ERROR "TSan is incompatible with ASan/UBSan. Use separate builds.")
    endif()

    if(CORO_SANITIZE_ASAN_AND_UBSAN)
        set(SANITIZERS ${SANITIZERS} -fsanitize=address,undefined)
    endif()

    if(CORO_SANITIZE_TSAN)
        set(SANITIZERS ${SANITIZERS} -fsanitize=thread)
    endif()

    if(SANITIZERS)
        target_compile_options(coro INTERFACE ${SANITIZERS})
        target_link_options(coro INTERFACE ${SANITIZERS})
    endif()

    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(third_party)
endif()
