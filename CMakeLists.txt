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
    target_compile_options(coro INTERFACE -Wall -Wextra -Wnewline-eof -Wformat -Werror)

    set(CORO_SANITIZE "off" CACHE STRING "Sanitize tests with 'address', 'leak' or 'thread'")
    set_property(CACHE CORO_SANITIZE PROPERTY STRINGS "off" "address" "leak" "thread")

    string(TOUPPER "${CORO_SANITIZE}" CORO_SANITIZE)

    set(SANITIZER_FLAGS "")

    if(CORO_SANITIZE STREQUAL "ADDRESS")
        message(STATUS "Enabling address sanitizer")
        set(SANITIZER_FLAGS -fsanitize=address,undefined)
    elseif(CORO_SANITIZE STREQUAL "LEAK")
        message(STATUS "Enabling leak sanitizer")
        set(SANITIZER_FLAGS -fsanitize=leak)
    elseif(CORO_SANITIZE STREQUAL "THREAD")
        message(STATUS "Enabling thread sanitizer")
        set(SANITIZER_FLAGS -fsanitize=thread)
    endif()

    if(SANITIZER_FLAGS)
        target_compile_options(coro INTERFACE ${SANITIZER_FLAGS})
        target_link_options(coro INTERFACE ${SANITIZER_FLAGS})
    endif()

    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(third_party)
endif()
