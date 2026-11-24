# TvShell.Controllers: see ../DEVELOPMENT.md.

qt_add_library(tvshell_controllers STATIC)

qt_add_qml_module(tvshell_controllers
    URI TvShell.Controllers
    QML_FILES
        ControllersPane.qml
        ControllerRow.qml
        ControllerTest.qml
        ControllerModel.qml
        PlayerLights.qml
    SOURCES
        ControllerClient.cpp
        ControllerClient.h
        MotionClient.cpp
        MotionClient.h
    DEPENDENCIES
        TARGET tvshell_common
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_controllers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_controllers
    PUBLIC
        Qt6::DBus
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        tvshell_common
)
