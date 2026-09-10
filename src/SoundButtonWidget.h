#pragma once

#include <QWidget>

class AudioEngine;
class SoundLibrary;
class QSystemTrayIcon;

// 桌面悬浮音效按钮：无边框置顶小窗，按下即播，拖动换位，滚轮换音效。
class SoundButtonWidget : public QWidget {
    Q_OBJECT
public:
    explicit SoundButtonWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void playCurrent();
    void addSounds();
    void removeCurrent();
    void toggleAlwaysOnTop();
    void makeTrayIcon();

    SoundLibrary *m_lib = nullptr;
    AudioEngine *m_audio = nullptr;
    QSystemTrayIcon *m_tray = nullptr;

    bool m_pressed = false;
    bool m_dragging = false;
    bool m_pendingPlay = false;   // 点击时还没解码完，就绪后自动补播
    QPoint m_pressGlobal;
    QPoint m_pressWindowTopLeft;
};
