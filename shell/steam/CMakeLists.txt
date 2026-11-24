# TvShell.Steam: see ../DEVELOPMENT.md.

qt_add_library(tvshell_steam STATIC)

qt_add_qml_module(tvshell_steam
    URI TvShell.Steam
    QML_FILES
        GamesPlace.qml
        GameTile.qml
    SOURCES
        SteamLibrary.cpp
        SteamLibrary.h
    DEPENDENCIES
        TARGET tvshell_common
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_steam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_steam
    PUBLIC
        Qt6::Qml
        Qt6::Quick
        tvshell_common
)
