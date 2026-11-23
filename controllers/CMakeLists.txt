cmake_minimum_required(VERSION 3.21)

project(tvcontrollers
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pinned to what SteamOS 3.9.5 ships (see toolchains/steamos-3.9).
find_package(Qt6 6.11 REQUIRED COMPONENTS Core DBus Network)

qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(tvcontrollers
    src/main.cpp
    src/Bluetooth.cpp
    src/Bluetooth.h
    src/ControllerService.cpp
    src/ControllerService.h
    src/DeviceWatcher.cpp
    src/DeviceWatcher.h
    protocol/emulator.h
    src/EmulatorServer.cpp
    src/EmulatorServer.h
    src/JoyCon.cpp
    src/JoyCon.h
    src/JoyConProtocol.h
    src/MotionCalibration.cpp
    src/MotionCalibration.h
    src/MotionServer.cpp
    src/MotionServer.h
    src/Unit.cpp
    src/Unit.h
    src/UnitManager.cpp
    src/UnitManager.h
    src/VirtualPad.cpp
    src/VirtualPad.h
)

target_link_libraries(tvcontrollers
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Network
)
