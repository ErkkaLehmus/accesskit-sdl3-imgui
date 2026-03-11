cmake_minimum_required(VERSION 4.0)
project(test_accesskit_sdl3_imgui)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(accesskit_DIR ${CMAKE_CURRENT_LIST_DIR}/external/accesskit-c/)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_package(accesskit REQUIRED)

# -------------- IMGUI static library creation`
#
set(IMGUI_PATH ${CMAKE_CURRENT_LIST_DIR}/external/imgui)
file(GLOB IMGUI_GLOB
        ${IMGUI_PATH}/imgui.h
        ${IMGUI_PATH}/imgui.cpp
        ${IMGUI_PATH}/imconfig.h
        ${IMGUI_PATH}/imgui_draw.cpp
        ${IMGUI_PATH}/imgui_internal.h
        ${IMGUI_PATH}/imstb_rectpack.h
        ${IMGUI_PATH}/imstb_textedit.h
        ${IMGUI_PATH}/imstb_truetype.h
        ${IMGUI_PATH}/imgui_tables.cpp
        ${IMGUI_PATH}/imgui_widgets.cpp

        # specific bindings...
        ${IMGUI_PATH}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_PATH}/backends/imgui_impl_sdlrenderer3.cpp)
add_library("imgui" STATIC ${IMGUI_GLOB})
target_include_directories("imgui" PUBLIC ${IMGUI_PATH})
target_link_libraries("imgui" PRIVATE SDL3::SDL3)
# ----------------------------------------------------------------------

add_executable(test_accesskit_sdl3_imgui main.cpp)

target_include_directories(test_accesskit_sdl3_imgui PUBLIC imgui imgui/backends)

# Link SDL3
target_link_libraries(test_accesskit_sdl3_imgui PUBLIC SDL3::SDL3 imgui accesskit)