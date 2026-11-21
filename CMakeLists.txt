cmake_minimum_required(VERSION 3.16)
project(NUST_SEECS_HMS VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HMS_USE_VENDORED "Use vendored libraries in third_party/ instead of FetchContent" OFF)

# Ensure runtime directories exist in both source and build directories
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/backups")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/exports")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/backups")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/exports")

find_package(OpenGL REQUIRED)

include(FetchContent)

# -----------------------------------------------------------------------------
# 1. GLFW (3.4)
# -----------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

if(HMS_USE_VENDORED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt")
    message(STATUS "[HMS] Using vendored GLFW from third_party/glfw")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw" EXCLUDE_FROM_ALL)
else()
    message(STATUS "[HMS] Fetching GLFW 3.4 via FetchContent")
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

# -----------------------------------------------------------------------------
# 2. Dear ImGui (docking branch: v1.91.5-docking)
# -----------------------------------------------------------------------------
if(HMS_USE_VENDORED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.h")
    message(STATUS "[HMS] Using vendored Dear ImGui from third_party/imgui")
    set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
else()
    message(STATUS "[HMS] Fetching Dear ImGui v1.91.5-docking via FetchContent")
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5-docking
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _nonexistent_cmake_subdir_
    )
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")
endif()

add_library(imgui STATIC
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_demo.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui SYSTEM PUBLIC
    "${IMGUI_SOURCE_DIR}"
    "${IMGUI_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imgui PRIVATE -w)
endif()

# -----------------------------------------------------------------------------
# 3. ImPlot (v0.16)
# -----------------------------------------------------------------------------
if(HMS_USE_VENDORED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot/implot.h")
    message(STATUS "[HMS] Using vendored ImPlot from third_party/implot")
    set(IMPLOT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot")
else()
    message(STATUS "[HMS] Fetching ImPlot v0.16 via FetchContent")
    FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG        v0.16
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _nonexistent_cmake_subdir_
    )
    FetchContent_MakeAvailable(implot)
    set(IMPLOT_SOURCE_DIR "${implot_SOURCE_DIR}")
endif()

add_library(implot STATIC
    "${IMPLOT_SOURCE_DIR}/implot.cpp"
    "${IMPLOT_SOURCE_DIR}/implot_items.cpp"
)

target_include_directories(implot SYSTEM PUBLIC "${IMPLOT_SOURCE_DIR}")
target_link_libraries(implot PUBLIC imgui)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(implot PRIVATE -w)
endif()

# -----------------------------------------------------------------------------
# 4. Application Executable: NUST_SEECS_HMS
# -----------------------------------------------------------------------------
set(HMS_SOURCES
    src/main.cpp
    src/Theme.cpp
    src/Sha256.cpp
    src/FileManager.cpp
    src/Algorithms.cpp
    src/HotelSystem.cpp
    src/GuiRenderer.cpp
    src/Gui_Dashboard.cpp
    src/Gui_Rooms.cpp
    src/Gui_Operations.cpp
    src/Gui_Admin.cpp
)

add_executable(NUST_SEECS_HMS ${HMS_SOURCES})

target_include_directories(NUST_SEECS_HMS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(NUST_SEECS_HMS PRIVATE
    imgui
    implot
    glfw
    OpenGL::GL
)

if(MSVC)
    target_compile_options(NUST_SEECS_HMS PRIVATE /W4 /utf-8)
else()
    target_compile_options(NUST_SEECS_HMS PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(NUST_SEECS_HMS PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
