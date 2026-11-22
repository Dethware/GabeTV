cmake_minimum_required(VERSION 3.21)

project(tvshell
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pinned to what SteamOS 3.9.5 ships (see toolchains/steamos-3.9).
find_package(Qt6 6.11 REQUIRED COMPONENTS DBus Gui Multimedia Network Quick WebEngineQuick)
find_package(SDL3 3.4 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xtest)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
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
set_source_files_properties(qml/PhotoLayer.qml PROPERTIES QT_RESOURCE_ALIAS PhotoLayer.qml)
set_source_files_properties(qml/ScreensaverLayer.qml PROPERTIES QT_RESOURCE_ALIAS ScreensaverLayer.qml)
set_source_files_properties(qml/AlbumPicker.qml PROPERTIES QT_RESOURCE_ALIAS AlbumPicker.qml)
set_source_files_properties(qml/PhotoBackdrop.qml PROPERTIES QT_RESOURCE_ALIAS PhotoBackdrop.qml)
set_source_files_properties(qml/PhotoInfo.qml PROPERTIES QT_RESOURCE_ALIAS PhotoInfo.qml)
set_source_files_properties(qml/FitGrid.qml PROPERTIES QT_RESOURCE_ALIAS FitGrid.qml)
set_source_files_properties(qml/AppTile.qml PROPERTIES QT_RESOURCE_ALIAS AppTile.qml)
set_source_files_properties(qml/YouTubeLogo.qml PROPERTIES QT_RESOURCE_ALIAS YouTubeLogo.qml)
set_source_files_properties(qml/JellyfinLogo.qml PROPERTIES QT_RESOURCE_ALIAS JellyfinLogo.qml)
set_source_files_properties(qml/VolumeIndicator.qml PROPERTIES QT_RESOURCE_ALIAS VolumeIndicator.qml)
set_source_files_properties(qml/NowPlayingButton.qml PROPERTIES QT_RESOURCE_ALIAS NowPlayingButton.qml)
set_source_files_properties(qml/ChoiceDialog.qml PROPERTIES QT_RESOURCE_ALIAS ChoiceDialog.qml)
set_source_files_properties(qml/sponsorskip.js PROPERTIES QT_RESOURCE_ALIAS sponsorskip.js)
set_source_files_properties(qml/mediacaps.js PROPERTIES QT_RESOURCE_ALIAS mediacaps.js)
set_source_files_properties(qml/HomePlace.qml PROPERTIES QT_RESOURCE_ALIAS HomePlace.qml)
set_source_files_properties(qml/YouTubePage.qml PROPERTIES QT_RESOURCE_ALIAS YouTubePage.qml)

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
        qml/PhotoLayer.qml
        qml/ScreensaverLayer.qml
        qml/AlbumPicker.qml
        qml/PhotoBackdrop.qml
        qml/PhotoInfo.qml
        qml/FitGrid.qml
        qml/AppTile.qml
        qml/YouTubeLogo.qml
        qml/JellyfinLogo.qml
        qml/VolumeIndicator.qml
        qml/NowPlayingButton.qml
        qml/ChoiceDialog.qml
        qml/HomePlace.qml
        qml/YouTubePage.qml
    RESOURCES
        qml/sponsorskip.js
        qml/mediacaps.js
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
        src/ImmichMediaProxy.cpp
        src/ImmichMediaProxy.h
        src/NetworkAccess.cpp
        src/NetworkAccess.h
        src/PlaceLookup.cpp
        src/PlaceLookup.h
        src/PowerActions.cpp
        src/PowerActions.h
        src/RemoteAssistant.cpp
        src/RemoteAssistant.h
        src/ScreensaverSettings.cpp
        src/ScreensaverSettings.h
        src/SteamLibrary.cpp
        src/SteamLibrary.h
        src/SteamLog.cpp
        src/SteamLog.h
        src/SponsorBlock.cpp
        src/SponsorBlock.h
        src/SystemVolume.cpp
        src/SystemVolume.h
        src/ViewerSettings.cpp
        src/ViewerSettings.h
        src/ManagedApp.cpp
        src/ManagedApp.h
        src/ShellService.cpp
        src/ShellService.h
        src/WebKeys.cpp
        src/WebKeys.h
        src/YouTubeSettings.cpp
        src/YouTubeSettings.h
)

# The generated QML type registration includes the headers by name.
target_include_directories(tvshell PRIVATE src)

target_link_libraries(tvshell
    PRIVATE
        Qt6::DBus
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Network
        Qt6::Quick
        Qt6::WebEngineQuick
        SDL3::SDL3
        PkgConfig::XCB
        PkgConfig::PULSE
        miniaudio
)
