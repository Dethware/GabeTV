# TvShell.Power: see ../DEVELOPMENT.md.

qt_add_library(tvshell_power STATIC)

qt_add_qml_module(tvshell_power
    URI TvShell.Power
    QML_FILES
        PowerDialog.qml
    SOURCES
        IdleController.cpp
        IdleController.h
        PowerActions.cpp
        PowerActions.h
        RemoteLink.cpp
        RemoteLink.h
        TvPower.cpp
        TvPower.h
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_power PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_power
    PUBLIC
        Qt6::DBus
        Qt6::Qml
        Qt6::Quick
)
