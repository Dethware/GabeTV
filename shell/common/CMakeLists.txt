# TvShell.Common: see ../DEVELOPMENT.md.

qt_add_library(tvshell_common STATIC)

qt_add_qml_module(tvshell_common
    URI TvShell.Common
    QML_FILES
        ChoiceDialog.qml
        FitGrid.qml
        BarButton.qml
        SettingRow.qml
    SOURCES
        NetworkAccess.cpp
        NetworkAccess.h
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_common
    PUBLIC
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
)
