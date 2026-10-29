cmake_minimum_required(VERSION 3.20)
project(fpsmon CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fpsmon WIN32
    src/main.cpp
    src/overlay.cpp
    src/frame_source.cpp
    src/sysinfo.cpp
)

target_include_directories(fpsmon PRIVATE src)

target_link_libraries(fpsmon PRIVATE
    advapi32
    user32
    gdi32
    shell32
    ole32
    d2d1
    dwrite
    pdh
)

if (MSVC)
    target_compile_options(fpsmon PRIVATE /W3 /utf-8)
    target_compile_definitions(fpsmon PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    set_target_properties(fpsmon PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()
