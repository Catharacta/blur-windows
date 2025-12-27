cmake_minimum_required(VERSION 3.20)
project(BlurWindowLibrary
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Custom blur window library for Windows 11"
)

# C++17/20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows SDK version
set(CMAKE_SYSTEM_VERSION 10.0.19041.0)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(BLURWINDOW_BUILD_DEMOS "Build demo applications" ON)
option(BLURWINDOW_BUILD_TESTS "Build tests" ON)
option(BLURWINDOW_ENABLE_LOGGING "Enable logging support" ON)

# Find required packages
find_package(directx-headers CONFIG QUIET)

# Windows libraries
set(WINDOWS_LIBS
    d3d11
    dxgi
    dcomp
    windowscodecs
    dwmapi
    d3dcompiler
    winmm
)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files - Core
set(CORE_SOURCES
    src/core/BlurSystem.cpp
    src/core/BlurWindow.cpp
    src/core/ResourceManager.cpp
    src/core/Logger.cpp
    src/core/FullscreenRenderer.cpp
)

# Source files - Capture
set(CAPTURE_SOURCES
    src/capture/DXGICapture.cpp
)

# Source files - Effects
set(EFFECTS_SOURCES
    src/effects/EffectPipeline.cpp
    src/effects/GaussianBlur.cpp
    src/effects/KawaseBlur.cpp
    src/effects/BoxBlur.cpp
    src/effects/RadialBlur.cpp
)

# Source files - Presentation
set(PRESENTATION_SOURCES
    src/presentation/DirectCompPresenter.cpp
    src/presentation/ULWPresenter.cpp
)

# Source files - Bindings
set(BINDINGS_SOURCES
    src/bindings/c_api.cpp
)

# All library sources
set(LIB_SOURCES
    ${CORE_SOURCES}
    ${CAPTURE_SOURCES}
    ${EFFECTS_SOURCES}
    ${PRESENTATION_SOURCES}
    ${BINDINGS_SOURCES}
)

# Create shared library
add_library(blurwindow SHARED ${LIB_SOURCES})

# Create static library
add_library(blurwindow_static STATIC ${LIB_SOURCES})

# Set library properties
set_target_properties(blurwindow PROPERTIES
    OUTPUT_NAME "blurwindow"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

set_target_properties(blurwindow_static PROPERTIES
    OUTPUT_NAME "blurwindow_static"
)

# Define export macro for DLL
target_compile_definitions(blurwindow PRIVATE BLURWINDOW_EXPORTS)
target_compile_definitions(blurwindow PUBLIC BLURWINDOW_DLL)

# Link Windows libraries
target_link_libraries(blurwindow PRIVATE ${WINDOWS_LIBS})
target_link_libraries(blurwindow_static PRIVATE ${WINDOWS_LIBS})

# Include directories for targets
target_include_directories(blurwindow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(blurwindow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(blurwindow_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Compile options
if(MSVC)
    target_compile_options(blurwindow PRIVATE /W4 /WX-)
    target_compile_options(blurwindow_static PRIVATE /W4 /WX-)
    
    # Enable debug info in Release for profiling
    target_compile_options(blurwindow PRIVATE $<$<CONFIG:Release>:/Zi>)
endif()

# Demo applications
if(BLURWINDOW_BUILD_DEMOS)
    add_subdirectory(demos/cpp_demo)
    add_subdirectory(demos/gui_demo)
endif()

# Tests
if(BLURWINDOW_BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)
endif()

# Install rules
install(TARGETS blurwindow blurwindow_static
    EXPORT BlurWindowTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/blurwindow
    DESTINATION include
)

# Export package
install(EXPORT BlurWindowTargets
    FILE BlurWindowTargets.cmake
    NAMESPACE BlurWindow::
    DESTINATION lib/cmake/BlurWindow
)
