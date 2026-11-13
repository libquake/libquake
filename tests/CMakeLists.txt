project(
    quakelib_unit_tests
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

include(FetchContent)

FetchContent_Declare(snitch
    GIT_REPOSITORY https://github.com/snitch-org/snitch.git
    GIT_TAG v1.3.2) # update version number as needed
FetchContent_MakeAvailable(snitch)

set(YOUR_TEST_FILES
    src/bsp_tests.cpp
    src/map_tests.cpp
    src/map_builder_tests.cpp
    src/wrapper_tests.cpp
    src/wad_tests.cpp
)

add_executable(${PROJECT_NAME} ${YOUR_TEST_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE snitch::snitch quakelib)

# Copy test data files to build directory
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    ${CMAKE_BINARY_DIR}/tests/data
    COMMENT "Copying test data files"
)

# Windows looks for dlls next to the executable
if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:quakelib> $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

add_custom_command(
    TARGET ${PROJECT_NAME}
    COMMENT "RUN QUAKELIB TESTS"
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMAND ${PROJECT_NAME}
)
