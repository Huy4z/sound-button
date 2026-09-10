#pragma once

#include <QPixmap>
#include <QVector>
#include <QWidget>

class AudioEngine;
class SoundLibrary;
class QSystemTrayIcon;
class QVariantAnimation;

// 桌面悬浮音效按钮：整个界面只有一个 QQ emoji 按钮 + 右上角图钉。
//  - 左键按下：播音效，并播放"手指按下"动画；按住不续播、松开瞬时复位（无回弹动画）
//  - 图钉：切换窗口置顶（实心=置顶，空心=不置顶）
//  - 拖动：移动位置；右键：全部设置都在菜单里（切换音效走菜单里的列表）
// 它同时是整个应用的外壳：持有数据层（SoundLibrary）与播放层（AudioEngine），
// 并负责托盘图标；不直接接触音频 API。
class SoundButtonWidget : public QWidget {
    Q_OBJECT
public:
    explicit SoundButtonWidget(QWidget *parent = nullptr);

protected:
    // 界面全部自绘：没有 .ui 文件，也不依赖系统图标主题
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;   // 按下即播（低延迟的关键）
    void mouseMoveEvent(QMouseEvent *event) override;    // 超过阈值判定拖动，停掉误播
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;   // 右键菜单

private:
    void playCurrent();        // 播放当前音效；没解码完就挂起，就绪后自动补播
    void prepareFormats();     // 把列表里所有已解码格式的播放流预热好
    void addSounds();          // 文件对话框选音效
    void removeCurrent();      // 移除当前音效
    void toggleAlwaysOnTop();  // 切换置顶（改 flag 会隐藏窗口，需重新 show）
    void makeTrayIcon();       // 托盘图标 + 菜单
    void updateToolTip();      // 悬停提示：当前音效 + 操作说明
    void flashName();          // 切换音效时短暂弹出名字（窗口里已经没有文字了）

    QRect buttonRect() const;   // 按钮绘制区域
    QRect pinRect() const;      // 图钉徽标：绘制与命中判定共用同一个矩形
    void ensureAssets();        // 按当前 DPI 缩放素材（换了屏幕/缩放就重建）
    void drawPin(QPainter &painter);
    void animatePress();   // 按下动画（唯一触发点）：正放到完全按下
    void resetPress();     // 松开/拖动：瞬时复位到未按下姿态，不播动画

    SoundLibrary *m_lib = nullptr;    // 数据层：列表、后台解码、config.json
    AudioEngine *m_audio = nullptr;   // 播放层：热流池、推流、音量
    QSystemTrayIcon *m_tray = nullptr;
    QVariantAnimation *m_pressAnim = nullptr;

    QVector<QPixmap> m_frames;   // 按下动画的 10 帧（0=松开姿态 9=完全按下）
    QPixmap m_pinOn;             // 图钉：实心（已置顶）
    QPixmap m_pinOff;            // 图钉：空心（未置顶）
    qreal m_assetsDpr = 0;       // 素材缓存对应的设备像素比

    bool m_pressed = false;       // 左键是否按下
    bool m_pinPressed = false;    // 本次按下落在图钉上（只切置顶，不播音效）
    bool m_dragging = false;      // 本次按下是否已升级为拖动
    bool m_pendingPlay = false;   // 点击时还没解码完，就绪后自动补播
    bool m_warmUpDone = false;    // 启动预热是否已完成（之后新格式随手预热）
    qint64 m_pressUs = 0;         // 鼠标按下时刻（诊断用）
    QPoint m_pressGlobal;         // 按下时的全局鼠标位置（逻辑坐标）
    QPoint m_pressWindowTopLeft;  // 按下时的窗口左上角，拖动时按位移量移动
};
