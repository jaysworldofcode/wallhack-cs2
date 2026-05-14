cmake_minimum_required(VERSION 3.20)
project(cs2_overlay CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Require Windows 10 SDK (for Direct2D 1.1, DWM, etc.)
add_definitions(-D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE -DNOMINMAX)

set(SOURCES
    src/main.cpp
    src/memory/memory.cpp
    src/math/math_utils.cpp
    src/game/entity.cpp
    src/overlay/overlay.cpp
    src/renderer/renderer.cpp
)

add_executable(cs2_overlay WIN32 ${SOURCES})

target_include_directories(cs2_overlay PRIVATE
    src
)

# Windows SDK libraries required
target_link_libraries(cs2_overlay PRIVATE
    d2d1          # Direct2D
    dwrite        # DirectWrite (text rendering)
    dwmapi        # Desktop Window Manager (DWM transparency)
    d3d11         # Direct3D 11 (backing D2D device)
    dxgi          # DXGI swap chain
    psapi         # Process Status API (EnumProcessModules)
)

# Treat warnings as errors in Debug for clean code
if(MSVC)
    target_compile_options(cs2_overlay PRIVATE
        $<$<CONFIG:Debug>:/W4>
        $<$<CONFIG:Release>:/O2 /W3>
    )
endif()
