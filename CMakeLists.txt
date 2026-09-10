cmake_minimum_required(VERSION 3.21)
project(SoundButton VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia)

qt_standard_project_setup()

qt_add_executable(soundbutton WIN32
    src/main.cpp
    src/SoundLibrary.h src/SoundLibrary.cpp
    src/AudioEngine.h src/AudioEngine.cpp
    src/SoundButtonWidget.h src/SoundButtonWidget.cpp
)

target_link_libraries(soundbutton PRIVATE Qt6::Widgets Qt6::Multimedia)
