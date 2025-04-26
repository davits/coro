include(CTest)

add_executable(simple simple.cpp)
target_link_libraries(simple coro gtest_main)
add_test(NAME simple COMMAND simple)
set_tests_properties(simple PROPERTIES TIMEOUT 2)

add_executable(stop stop.cpp)
target_link_libraries(stop coro gtest_main)
add_test(NAME stop COMMAND stop)
set_tests_properties(stop PROPERTIES TIMEOUT 2)

add_executable(timeout timeout.cpp)
target_link_libraries(timeout coro gtest_main)
add_test(NAME timeout COMMAND timeout)
set_tests_properties(timeout PROPERTIES TIMEOUT 2)

add_executable(coro_all coro_all.cpp)
target_link_libraries(coro_all coro gtest_main)
add_test(NAME coro_all COMMAND coro_all)
set_tests_properties(coro_all PROPERTIES TIMEOUT 2)

add_executable(latch latch.cpp)
target_link_libraries(latch coro gtest_main)
add_test(NAME latch COMMAND latch)
set_tests_properties(latch PROPERTIES TIMEOUT 2)

add_executable(mutex mutex.cpp)
target_link_libraries(mutex coro gtest_main)
add_test(NAME mutex COMMAND mutex)
set_tests_properties(mutex PROPERTIES TIMEOUT 2)

add_executable(lifetime lifetime.cpp)
target_link_libraries(lifetime coro gtest_main)
add_test(NAME lifetime COMMAND lifetime)
set_tests_properties(lifetime PROPERTIES TIMEOUT 2)

add_executable(cross cross.cpp)
target_link_libraries(cross coro gtest_main)
add_test(NAME cross COMMAND cross)
set_tests_properties(cross PROPERTIES TIMEOUT 2)
