set(CMAKE_BUILD_TYPE "Debug")

cmake_minimum_required(VERSION 3.21)
project(integrate-imgui-cglm-sdl3-webgpu-cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# ImGui Path & Source Definitions
# -----------------------------------------------------------------------------
set(IMGUI_DIR "C:/libs/imgui-master")

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_wgpu.cpp
)

# -----------------------------------------------------------------------------
# Define Project Source Files
# -----------------------------------------------------------------------------
set(PROJECT_SOURCES
    src/main.cpp
    src/webgpu_context.h
    src/webgpu_context.cpp
    ${IMGUI_SOURCES}
)

# -----------------------------------------------------------------------------
# Declare Target Type based on OS
# -----------------------------------------------------------------------------
if(ANDROID)
    # Android requires a shared library (.so) for SDLActivity
    add_library(app SHARED)
else()
    # Windows & Emscripten require an executable target
    add_executable(app)
endif()

# Attach sources to the target dynamically
target_sources(app PRIVATE ${PROJECT_SOURCES})

# -----------------------------------------------------------------------------
# Include Global Header Dependencies
# -----------------------------------------------------------------------------
target_include_directories(app PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    C:/libs/cglm-0.9.6/include
)

# -----------------------------------------------------------------------------
# Platform Linking & Dependencies
# -----------------------------------------------------------------------------
if(EMSCRIPTEN)
    # Emscripten / WebAssembly Build Settings
    add_library(SDL3_Static STATIC IMPORTED)
    set_target_properties(SDL3_Static PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-3.4.16-wasm/lib/libSDL3.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-3.4.16-wasm/include"
    )

    target_link_libraries(app PRIVATE
        SDL3_Static
    )

    target_compile_options(app PRIVATE
        -Os
        --use-port=emdawnwebgpu
    )

    target_link_options(app PRIVATE
        "-Os"
        "-sMALLOC=emmalloc"
        "-sUSE_SDL=0"
        "--use-port=emdawnwebgpu" # Enables browser WebGPU JS support
    )

elseif(ANDROID)
    target_compile_definitions(app PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

    add_library(SDL3 SHARED IMPORTED)
    set_target_properties(SDL3 PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-3.4.16-android/${ANDROID_ABI}/lib/libSDL3.so"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-3.4.16-android/${ANDROID_ABI}/include"
    )

    add_library(wgpu_native STATIC IMPORTED)
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "C:/libs/wgpu-29.0.1.1-android/${ANDROID_ABI}/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/wgpu-29.0.1.1-android/${ANDROID_ABI}/include"
    )

    find_library(ANDROID_LIB android REQUIRED)
    find_library(LOG_LIB log REQUIRED)

    target_link_libraries(app PRIVATE
        SDL3
        wgpu_native
        ${ANDROID_LIB}
        ${LOG_LIB}
    )

elseif(WIN32)
    add_compile_definitions(IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

    add_library(SDL3_Static STATIC IMPORTED)
    set_target_properties(SDL3_Static PROPERTIES
        IMPORTED_LOCATION "C:/libs/sdl3-3.4.16-mingw/lib/libSDL3.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/sdl3-3.4.16-mingw/include"
        INTERFACE_LINK_LIBRARIES "winmm;imm32;version;setupapi;dinput8"
    )

    add_library(wgpu_native STATIC IMPORTED)
    set_target_properties(wgpu_native PROPERTIES
        IMPORTED_LOCATION "C:/libs/wgpu-29.0.1.1-mingw/lib/libwgpu_native.a"
        INTERFACE_INCLUDE_DIRECTORIES "C:/libs/wgpu-29.0.1.1-mingw/include"
        INTERFACE_LINK_LIBRARIES "ws2_32;userenv;ntdll;bcrypt;d3d12;dxgi"
    )

    target_link_libraries(app PRIVATE
        SDL3_Static
        wgpu_native
    )

    # Strip symbols and optimize aggressive for size
    target_compile_options(app PRIVATE -Os)

    # MinGW Static Build Settings for Windows
    target_link_options(app PRIVATE -static -s -static-libgcc -static-libstdc++)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(app PRIVATE -mconsole)
    else()
        target_link_options(app PRIVATE -mwindows)
    endif()
endif()
