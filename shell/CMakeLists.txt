cmake_minimum_required(VERSION 3.21)

project(tvshell
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pinned to what SteamOS 3.9.5 ships (see toolchains/steamos-3.9).
find_package(Qt6 6.11 REQUIRED COMPONENTS DBus Gui Network Quick)
find_package(SDL3 3.4 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xtest)
find_package(Threads REQUIRED)

qt_standard_project_setup(REQUIRES 6.11)

# Vendored (3rdparty/miniaudio/README.md). Audio backends are loaded at runtime.
add_library(miniaudio STATIC
    3rdparty/miniaudio/miniaudio.c
)
target_include_directories(miniaudio
    SYSTEM PUBLIC
        3rdparty/miniaudio
)
target_link_libraries(miniaudio
    PRIVATE
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
)

qt_add_executable(tvshell
    src/main.cpp
)

# Keep the QML files at the module root in resources, without the qml/ prefix.
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/GamesPlace.qml PROPERTIES QT_RESOURCE_ALIAS GamesPlace.qml)
set_source_files_properties(qml/GameTile.qml PROPERTIES QT_RESOURCE_ALIAS GameTile.qml)
set_source_files_properties(qml/Screensaver.qml PROPERTIES QT_RESOURCE_ALIAS Screensaver.qml)
set_source_files_properties(qml/PowerDialog.qml PROPERTIES QT_RESOURCE_ALIAS PowerDialog.qml)
set_source_files_properties(qml/SettingRow.qml PROPERTIES QT_RESOURCE_ALIAS SettingRow.qml)
set_source_files_properties(qml/SettingsPage.qml PROPERTIES QT_RESOURCE_ALIAS SettingsPage.qml)
set_source_files_properties(qml/TopBar.qml PROPERTIES QT_RESOURCE_ALIAS TopBar.qml)
set_source_files_properties(qml/AssistantIndicator.qml PROPERTIES QT_RESOURCE_ALIAS AssistantIndicator.qml)
set_source_files_properties(qml/PhotoViewer.qml PROPERTIES QT_RESOURCE_ALIAS PhotoViewer.qml)
set_source_files_properties(qml/PhotoGrid.qml PROPERTIES QT_RESOURCE_ALIAS PhotoGrid.qml)
set_source_files_properties(qml/PhotoTile.qml PROPERTIES QT_RESOURCE_ALIAS PhotoTile.qml)
set_source_files_properties(qml/PhotosPlace.qml PROPERTIES QT_RESOURCE_ALIAS PhotosPlace.qml)
set_source_files_properties(qml/BarButton.qml PROPERTIES QT_RESOURCE_ALIAS BarButton.qml)

qt_add_qml_module(tvshell
    URI TvShell
    QML_FILES
        qml/Main.qml
        qml/GamesPlace.qml
        qml/GameTile.qml
        qml/Screensaver.qml
        qml/PowerDialog.qml
        qml/SettingRow.qml
        qml/SettingsPage.qml
        qml/TopBar.qml
        qml/AssistantIndicator.qml
        qml/PhotoViewer.qml
        qml/PhotoGrid.qml
        qml/PhotoTile.qml
        qml/PhotosPlace.qml
        qml/BarButton.qml
    SOURCES
        src/AudioPlayer.cpp
        src/AudioPlayer.h
        src/BaseLayer.cpp
        src/BaseLayer.h
        src/GamepadInput.cpp
        src/GamepadInput.h
        src/ImaAdpcm.cpp
        src/ImaAdpcm.h
        src/IdleController.cpp
        src/IdleController.h
        src/ImmichClient.cpp
        src/ImmichClient.h
        src/ImmichConfig.cpp
        src/ImmichConfig.h
        src/NetworkAccess.cpp
        src/NetworkAccess.h
        src/PowerActions.cpp
        src/PowerActions.h
        src/RemoteAssistant.cpp
        src/RemoteAssistant.h
        src/SteamLibrary.cpp
        src/SteamLibrary.h
        src/SteamLog.cpp
        src/SteamLog.h
)

# The generated QML type registration includes the headers by name.
target_include_directories(tvshell PRIVATE src)

target_link_libraries(tvshell
    PRIVATE
        Qt6::DBus
        Qt6::Gui
        Qt6::Network
        Qt6::Quick
        SDL3::SDL3
        PkgConfig::XCB
        miniaudio
)
