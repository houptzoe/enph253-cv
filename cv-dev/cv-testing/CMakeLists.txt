cmake_minimum_required(VERSION 3.16)
project(mars-cv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs videoio)

add_executable(mars-cv src/main.cpp)
target_include_directories(mars-cv PRIVATE include)
target_link_libraries(mars-cv PRIVATE ${OpenCV_LIBS})

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(STATUS "Target: ARM64 (Raspberry Pi)")
    target_compile_definitions(mars-cv PRIVATE MARS_CV_RPI=1)
endif()

if(WIN32)
    add_custom_command(TARGET mars-cv POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:mars-cv>
            $<TARGET_FILE_DIR:mars-cv>
        COMMAND_EXPAND_LISTS
    )
    set_target_properties(mars-cv PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

install(TARGETS mars-cv RUNTIME DESTINATION bin)
