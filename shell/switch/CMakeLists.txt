# TvShell.Switch: see ../DEVELOPMENT.md.

# The file formats, on their own: plain C++ on Qt Core, for the module and for the tool.
add_library(tvshell_switch_format STATIC
    format/Aes.cpp
    format/Aes.h
    format/GameFile.cpp
    format/GameFile.h
    format/Keys.cpp
    format/Keys.h
    format/Nacp.cpp
    format/Nacp.h
    format/Nca.cpp
    format/Nca.h
    format/PartitionFs.cpp
    format/PartitionFs.h
    format/RomFs.cpp
    format/RomFs.h
    format/Storage.cpp
    format/Storage.h
)
target_include_directories(tvshell_switch_format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tvshell_switch_format
    PUBLIC
        Qt6::Core
    PRIVATE
        OpenSSL::Crypto
        ZLIB::ZLIB
)

qt_add_library(tvshell_switch STATIC)

qt_add_qml_module(tvshell_switch
    URI TvShell.Switch
    SOURCES
        GameCache.cpp
        GameCache.h
        SwitchGame.cpp
        SwitchGame.h
)

# The generated type registration includes the headers by name.
target_include_directories(tvshell_switch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tvshell_switch
    PUBLIC
        Qt6::Qml
        Qt6::Quick
    PRIVATE
        tvshell_switch_format
)

# What the shell reads from a game file, without the shell (tools/switch-gameinfo.cpp).
qt_add_executable(switch-gameinfo
    tools/switch-gameinfo.cpp
)
target_link_libraries(switch-gameinfo
    PRIVATE
        tvshell_switch_format
)
