# Add the target
set(MODULE_NAME coro_tests)
add_executable(${MODULE_NAME} bindings.cpp)

target_link_libraries(${MODULE_NAME} PRIVATE coro)

# Add Embind-specific flags
target_compile_options(${MODULE_NAME} PRIVATE -fwasm-exceptions)
target_link_options(${MODULE_NAME} PRIVATE
    -sMODULARIZE=1
    -sENVIRONMENT=node
    -sALLOW_MEMORY_GROWTH=1
    -sEXPORT_NAME=createModule
    -lembind
    -fwasm-exceptions
    --no-entry
)

add_custom_command(
    TARGET ${MODULE_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/${MODULE_NAME}.js
            ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}.js
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/${MODULE_NAME}.wasm
            ${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_NAME}.wasm
    COMMENT "Copying built files to the tests directory."
)

add_test(
    NAME coro_tests
    COMMAND npm run test 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
