cmake_minimum_required(VERSION 3.21)

project(coro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CORO_TESTS "Enable tests." OFF)

# Create dummy file to be able to create a STATIC library
set(DUMMY_FILE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp)
if(NOT EXISTS ${DUMMY_FILE})
    file(TOUCH ${DUMMY_FILE})
endif()

# Main library of this project
add_library(coro STATIC ${DUMMY_FILE})

if(CORO_TESTS)
    target_include_directories(coro INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
else()
    target_include_directories(coro SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

if(EMSCRIPTEN)
    target_link_options(coro PUBLIC --js-library ${CMAKE_CURRENT_SOURCE_DIR}/include/coro/emscripten/coro_library.js)
    target_sources(coro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/coro/emscripten/coro_library.cpp)
endif()

if(EMSCRIPTEN)
    target_compile_definitions(coro PUBLIC CORO_EMSCRIPTEN)
endif()

if(CORO_TESTS)
    option(CORO_SANITIZE_ASAN_AND_UBSAN "Enable ASan and UBSan." ON)
    option(CORO_SANITIZE_TSAN "Enable TSan." OFF)

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
