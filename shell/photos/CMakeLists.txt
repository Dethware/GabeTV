# TvShell.Photos: see ../DEVELOPMENT.md.

qt_add_library(tvshell_photos STATIC)

qt_add_qml_module(tvshell_photos
    URI TvShell.Photos
    QML_FILES
        AlbumGroups.qml
        AlbumPicker.qml
        PhotoBackdrop.qml
        PhotoGrid.qml
        PhotoInfo.qml
        PhotoLayer.qml
        PhotosPlace.qml
        PhotoTile.qml
        PhotoViewer.qml
        Screensaver.qml
        ScreensaverLayer.qml
    SOURCES
        ImmichClient.cpp
        ImmichClient.h
        ImmichConfig.cpp
        ImmichConfig.h
        ImmichMediaProxy.cpp
        ImmichMediaProxy.h
        PlaceLookup.cpp
        PlaceLookup.h
        ScreensaverSettings.cpp
        ScreensaverSettings.h
        ViewerSettings.cpp
        ViewerSettings.h
    DEPENDENCIES
        TARGET tvshell_common
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_photos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_photos
    PUBLIC
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::Multimedia
        tvshell_common
)
