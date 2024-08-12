include(CTest)

add_executable(simple simple.cpp)
target_link_libraries(simple coro gtest_main)
add_test(NAME simple COMMAND simple)
