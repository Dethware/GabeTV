# TvShell.YouTube: see ../DEVELOPMENT.md.

qt_add_library(tvshell_youtube STATIC)

qt_add_qml_module(tvshell_youtube
    URI TvShell.YouTube
    QML_FILES
        YouTubePage.qml
    RESOURCES
        sponsorskip.js
        mediacaps.js
    SOURCES
        SponsorBlock.cpp
        SponsorBlock.h
        YouTubeSettings.cpp
        YouTubeSettings.h
        WebKeys.cpp
        WebKeys.h
    DEPENDENCIES
        # QWindow, the type of their window property.
        QtQuick
        TARGET tvshell_common
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_youtube PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_youtube
    PUBLIC
        Qt6::Network
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::WebEngineQuick
        tvshell_common
)
