cmake_minimum_required(VERSION 3.21)

project(tvshell
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pinned to what SteamOS 3.9.5 ships (see toolchains/steamos-3.9).
find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Quick)
find_package(SDL3 3.4 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(tvshell
    src/main.cpp
)

# Keep the QML files at the module root in resources, without the qml/ prefix.
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/GamesPlace.qml PROPERTIES QT_RESOURCE_ALIAS GamesPlace.qml)
set_source_files_properties(qml/GameTile.qml PROPERTIES QT_RESOURCE_ALIAS GameTile.qml)

qt_add_qml_module(tvshell
    URI TvShell
    QML_FILES
        qml/Main.qml
        qml/GamesPlace.qml
        qml/GameTile.qml
    SOURCES
        src/BaseLayer.cpp
        src/BaseLayer.h
        src/GamepadInput.cpp
        src/GamepadInput.h
        src/SteamLibrary.cpp
        src/SteamLibrary.h
)

# The generated QML type registration includes the headers by name.
target_include_directories(tvshell PRIVATE src)

target_link_libraries(tvshell
    PRIVATE
        Qt6::Gui
        Qt6::Quick
        SDL3::SDL3
        PkgConfig::XCB
)
