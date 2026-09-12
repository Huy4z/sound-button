#include <QApplication>

#include "SoundButtonWidget.h"

// 程序入口：起 QApplication、显示唯一的悬浮按钮窗口、进事件循环。
// 音效列表、播放、托盘都由 SoundButtonWidget 自己持有，这里保持最小。
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("sound-button"));
    QApplication::setApplicationDisplayName(QStringLiteral("音效按钮"));

    // 构造里已经完成：读 config.json、启动后台解码、给输出设备排预热定时器
    SoundButtonWidget w;
    w.show();
    // 事件循环：鼠标按下/右键菜单/托盘点击都在这里派发；退出由菜单或托盘触发
    return QApplication::exec();
}
