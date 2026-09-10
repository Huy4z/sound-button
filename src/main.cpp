#include <QApplication>

#include "SoundButtonWidget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("sound-button"));
    QApplication::setApplicationDisplayName(QStringLiteral("音效按钮"));

    SoundButtonWidget w;
    w.show();
    return QApplication::exec();
}
