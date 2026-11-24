# TvShell.Assistant: see ../DEVELOPMENT.md.

qt_add_library(tvshell_assistant STATIC)

qt_add_qml_module(tvshell_assistant
    URI TvShell.Assistant
    QML_FILES
        AssistantIndicator.qml
    SOURCES
        RemoteAssistant.cpp
        RemoteAssistant.h
        ImaAdpcm.cpp
        ImaAdpcm.h
        AudioPlayer.cpp
        AudioPlayer.h
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_assistant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_assistant
    PUBLIC
        Qt6::DBus
        Qt6::Qml
        Qt6::Quick
        miniaudio
)
