include(CTest)

add_executable(simple simple.cpp)
target_link_libraries(simple coro gtest_main)
add_test(NAME simple COMMAND simple)

add_executable(stop stop.cpp)
target_link_libraries(stop coro gtest_main)
add_test(NAME stop COMMAND stop)

add_executable(timeout timeout.cpp)
target_link_libraries(timeout coro gtest_main)
add_test(NAME timeout COMMAND timeout)
