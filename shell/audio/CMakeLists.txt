# TvShell.Audio: see ../DEVELOPMENT.md.

qt_add_library(tvshell_audio STATIC)

qt_add_qml_module(tvshell_audio
    URI TvShell.Audio
    QML_FILES
        VolumeIndicator.qml
        NowPlayingButton.qml
    SOURCES
        SystemVolume.cpp
        SystemVolume.h
    DEPENDENCIES
        TARGET tvshell_apps
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_audio
    PUBLIC
        Qt6::Qml
        Qt6::Quick
        PkgConfig::PULSE
        tvshell_apps
)
