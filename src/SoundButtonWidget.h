#pragma once

#include <QPixmap>
#include <QVector>
#include <QWidget>

class AudioEngine;
class SoundLibrary;
class QVariantAnimation;

/// 桌面悬浮音效按钮（UI 层）：整个界面只有一个 QQ emoji 按钮 + 右上角图钉。
///  - 左键按下：播音效，并播放"手指按下"动画；按住不续播、松开瞬时复位（无回弹动画）
///    菜单里选「无」时不发声，动画照常
///  - 图钉：切换窗口置顶（实心蓝=置顶，空心灰=不置顶）
///  - 拖动：移动位置；右键：全部设置都在菜单里（切换音效走菜单里的列表）
/// 它同时是整个应用的外壳：持有数据层（SoundLibrary）与播放层（AudioEngine），
/// 并负责托盘图标；不直接接触音频 API。
class SoundButtonWidget : public QWidget {
    Q_OBJECT
public:
    explicit SoundButtonWidget(QWidget *parent = nullptr);

protected:
    /// 界面全部自绘：没有 .ui 文件，也不依赖系统图标主题
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;   ///< 按下即播（低延迟的关键）
    void mouseMoveEvent(QMouseEvent *event) override;    ///< 超过阈值判定拖动，停掉误播
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;   ///< 右键菜单

private:
    /// 播放当前音效；没解码完就挂起，就绪后自动补播
    void playCurrent();
    /// 把所有已解码格式的播放流预热好（首次调用会付设备枚举的固定成本）
    void prepareFormats();
    /// 文件对话框选音效（默认定位到 exe 旁 sounds/）
    void addSounds();
    /// 移除当前音效并落盘
    void removeCurrent();
    /// 切换置顶（改 flag 会隐藏窗口，需重新 show）；与右键菜单共用同一状态
    void toggleAlwaysOnTop();
    /// 托盘图标 + 菜单（左键单击 = 显示/隐藏按钮）
    void makeTrayIcon();
    /// 悬停提示：当前音效 + 操作说明
    void updateToolTip();
    /// 切换音效时短暂弹出名字（窗口里没有文字，用气泡给反馈）
    void flashName();

    QRect buttonRect() const;   ///< 按钮绘制区域
    QRect pinRect() const;      ///< 图钉徽标；绘制与命中判定共用同一个矩形
    void ensureAssets();        ///< 按当前 DPI 缩放素材（换屏幕/改缩放时重建缓存）
    void drawPin(QPainter &painter);  ///< 画图钉徽标（含按压反馈与置顶两态配色）
    void animatePress();   ///< 按下动画（唯一触发点）：正放到完全按下，按住不续播
    void resetPress();     ///< 松开/拖动：瞬时复位到未按下姿态，不播回弹

    SoundLibrary *m_lib = nullptr;    ///< 数据层：列表、后台解码、config.json
    AudioEngine *m_audio = nullptr;   ///< 播放层：热流池、推流、音量
    QVariantAnimation *m_pressAnim = nullptr;  ///< 按下动画（值 = 帧号）

    QVector<QPixmap> m_frames;   ///< 按下动画的 10 帧（0=松开姿态 9=完全按下）
    QPixmap m_pinOn;             ///< 图钉：实心（已置顶）
    QPixmap m_pinOff;            ///< 图钉：空心（未置顶）
    qreal m_assetsDpr = 0;       ///< 素材缓存对应的设备像素比；0 = 还没建缓存

    bool m_pressed = false;       ///< 左键是否按下
    bool m_pinPressed = false;    ///< 本次按下落在图钉上（只切置顶，不播音效）
    bool m_dragging = false;      ///< 本次按下是否已升级为拖动
    bool m_pendingPlay = false;   ///< 点击时还没解码完，就绪后自动补播
    bool m_warmUpDone = false;    ///< 启动预热是否已完成（之后新格式随手预热）
    qint64 m_pressUs = 0;         ///< 鼠标按下时刻（诊断用；playCurrent 消费后清零）
    QPoint m_pressGlobal;         ///< 按下时的全局鼠标位置（逻辑坐标）
    QPoint m_pressWindowTopLeft;  ///< 按下时的窗口左上角；拖动时按位移量移动
};
