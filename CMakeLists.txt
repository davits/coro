cmake_minimum_required(VERSION 3.21)

project(coro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CORO_TESTS "Enable tests." OFF)

add_library(coro INTERFACE)
target_include_directories(coro INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CORO_TESTS)
    target_compile_options(coro INTERFACE -Wall -Wextra -Wnewline-eof -Wformat -Werror)

    target_compile_options(coro INTERFACE -fsanitize=thread)
    target_link_options(coro INTERFACE -fsanitize=thread)

    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(third_party)
endif()
