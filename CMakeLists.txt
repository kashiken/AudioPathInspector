cmake_minimum_required(VERSION 3.25)

project(AudioPathInspector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

find_package(wxWidgets CONFIG REQUIRED)

add_executable(AudioPathInspector WIN32
    src/main.cpp
    src/audio/DeviceInspectionService.cpp
    src/app/App.cpp
    src/app/MainFrame.cpp
    src/windows/ApoReader.cpp
    src/windows/AudioEnhancementsReader.cpp
    src/windows/AudioEffectsReader.cpp
    src/windows/DeviceEnumerator.cpp
    src/windows/DeviceInfoReader.cpp
    src/windows/StreamOpenTester.cpp
)

target_include_directories(AudioPathInspector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(AudioPathInspector PRIVATE
    wx::base
    wx::core
    ole32
    propsys
    version
)
