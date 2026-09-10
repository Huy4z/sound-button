cmake_minimum_required(VERSION 3.21)
project(SoundButton VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia)

qt_standard_project_setup()

# WIN32 = 不带控制台窗口；依赖 Qt Widgets（界面）与 Multimedia（QAudioSink/QAudioDecoder）
qt_add_executable(soundbutton WIN32
    src/main.cpp
    src/LatencyLog.h
    src/SoundLibrary.h src/SoundLibrary.cpp
    src/AudioEngine.h src/AudioEngine.cpp
    src/SoundButtonWidget.h src/SoundButtonWidget.cpp
)

target_link_libraries(soundbutton PRIVATE Qt6::Widgets Qt6::Multimedia)

# 界面素材（QQ emoji 按钮的按下动画帧 + 图钉图标）编进 exe，运行时不需要外部文件
qt_add_resources(soundbutton "ui_assets"
    PREFIX "/assets"
    BASE "assets"
    FILES
        assets/button_0.png
        assets/button_1.png
        assets/button_2.png
        assets/button_3.png
        assets/button_4.png
        assets/button_5.png
        assets/button_6.png
        assets/button_7.png
        assets/button_8.png
        assets/button_9.png
        assets/pin.png
        assets/pin_fill.png
)
