cmake_minimum_required(VERSION 3.16)
project(mars-cv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs videoio dnn)

add_executable(mars-cv 
    src/main.cpp
    src/camera.cpp
    src/frame-source.cpp
    src/teletubby-detector.cpp
    src/esp-handshake.cpp
)

if(UNIX)
    target_sources(mars-cv PRIVATE src/mjpeg-stream.cpp)
    target_compile_definitions(mars-cv PRIVATE MARS_CV_MJPEG_STREAM=1)
endif()

target_include_directories(mars-cv PRIVATE include)
target_link_libraries(mars-cv PRIVATE ${OpenCV_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(mars-cv PRIVATE pthread)
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(STATUS "Target: ARM64 (Raspberry Pi)")
    target_compile_definitions(mars-cv PRIVATE MARS_CV_RPI=1)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GPIOD QUIET libgpiod)
    endif()
    if(GPIOD_FOUND)
        target_include_directories(mars-cv PRIVATE ${GPIOD_INCLUDE_DIRS})
        target_link_libraries(mars-cv PRIVATE ${GPIOD_LIBRARIES})
        message(STATUS "libgpiod: ${GPIOD_LIBRARIES}")
    else()
        find_library(GPIOD_LIBRARY gpiod)
        find_path(GPIOD_INCLUDE_DIR gpiod.h)
        if(NOT GPIOD_LIBRARY OR NOT GPIOD_INCLUDE_DIR)
            message(FATAL_ERROR
                "libgpiod not found. On the Pi run: sudo apt-get install -y libgpiod-dev")
        endif()
        target_include_directories(mars-cv PRIVATE ${GPIOD_INCLUDE_DIR})
        target_link_libraries(mars-cv PRIVATE ${GPIOD_LIBRARY})
        message(STATUS "libgpiod: ${GPIOD_LIBRARY}")
    endif()
endif()

if(WIN32)
    add_custom_command(TARGET mars-cv POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying OpenCV runtime DLLs (may take a minute)..."
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
