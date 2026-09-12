// UI 层实现：全自绘窗口、鼠标/菜单交互、预热与解码回调的调度。
#include "SoundButtonWidget.h"

#include "AudioEngine.h"
#include "SoundLibrary.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QMediaDevices>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolTip>
#include <QVariantAnimation>
#include <QWidgetAction>

namespace {
constexpr int kDragThreshold = 8;     // 移动超过这么多像素就判定为"拖动"而不是"点击"
constexpr int kWarmUpDelayMs = 300;   // 启动后多久开始预热播放流

constexpr int kButtonSize = 100;      // 按钮边长（逻辑像素）
constexpr int kWindowPad = 2;         // 按钮四周留一点余量给缩放抗锯齿
constexpr int kBadgeSize = 26;        // 图钉徽标直径
constexpr int kBadgeIconSize = 14;    // 图钉图标在徽标里的边长
constexpr int kFrameCount = 10;       // 按下动画帧数（素材是 9 帧 @60fps，多一帧收尾更自然）
constexpr int kPressMs = 150;         // 按下动画时长（动画只有"按下"这一个触发点）

// 把单色图标染成指定颜色（素材是黑色剪影，靠这个出两态配色）
QPixmap tinted(const QPixmap &src, const QColor &color) {
    QPixmap pm(src.size());
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.drawPixmap(0, 0, src);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    return pm;
}
}  // namespace

SoundButtonWidget::SoundButtonWidget(QWidget *parent) : QWidget(parent) {
    // ---- 依赖装配：数据层 + 播放层，先读配置（音量、位置、音效列表） ----
    m_lib = new SoundLibrary(this);
    m_audio = new AudioEngine(this);
    m_lib->loadConfig();

    // ---- 窗口外观：无边框 + 不进任务栏 + 置顶 + 透明背景，内容全部自绘 ----
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (m_lib->alwaysOnTop()) flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kButtonSize + kWindowPad * 2, kButtonSize + kWindowPad * 2);
    setWindowTitle(tr("音效按钮"));

    move(m_lib->windowPos());
    m_audio->setVolume(m_lib->volume());

    // 按下动画：值就是帧号，只有按下时正放到最后一帧；
    // 按住不续播、松开/拖动一律瞬时复位（见 resetPress），不做倒放回弹
    m_pressAnim = new QVariantAnimation(this);
    m_pressAnim->setStartValue(0.0);
    m_pressAnim->setEndValue(qreal(kFrameCount - 1));
    m_pressAnim->setDuration(kPressMs);
    m_pressAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_pressAnim, &QVariantAnimation::valueChanged, this,
            qOverload<>(&QWidget::update));

    // ---- 信号接线 ----
    connect(m_lib, &SoundLibrary::entriesChanged, this,
            qOverload<>(&QWidget::update));
    connect(m_lib, &SoundLibrary::currentChanged, this, [this] {
        // 手动切换音效时取消"就绪后补播"，否则会放错曲子
        m_pendingPlay = false;
        updateToolTip();
        if (isVisible()) flashName();   // 窗口里没有文字，切歌时弹一下名字
        update();
    });
    connect(m_lib, &SoundLibrary::entryReady, this, [this](int index) {
        // 之后才解码出来的新格式，也顺手预热（建流成本不在点击路径上）
        if (const SoundEntry *en = m_lib->entry(index); m_warmUpDone && en && en->ready)
            m_audio->prepare(en->format);
        // 解码慢、用户已经点过了：现在数据齐了，补上这次播放
        if (index == m_lib->currentIndex() && m_pendingPlay) {
            m_pendingPlay = false;
            playCurrent();
        }
        updateToolTip();
        update();
    });
    // 插拔耳机 / 切换默认输出：引擎自己会监听 audioOutputsChanged 丢弃旧热流
    // （见 AudioEngine 构造函数），这里只负责在新设备上把各格式重新预热
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this,
            [this] { prepareFormats(); });

    // 预热：设备枚举 + 建流 + 流初始化在本机合计约 1s（首次点击时付就是干等），
    // 这里推迟一拍执行，先让窗口显示出来
    QTimer::singleShot(kWarmUpDelayMs, this, [this] {
        m_warmUpDone = true;
        prepareFormats();
    });

    updateToolTip();
    makeTrayIcon();
}

QRect SoundButtonWidget::buttonRect() const {
    return QRect(kWindowPad, kWindowPad, kButtonSize, kButtonSize);
}

// 图钉压在按钮右上角：坐标与按钮区域部分重叠，但装饰本身在右上角是透明的
QRect SoundButtonWidget::pinRect() const {
    return QRect(width() - kWindowPad - kBadgeSize, 0, kBadgeSize, kBadgeSize);
}

// 素材按当前 DPI 缩放一次后缓存；换屏幕/改缩放（DPR 变化）时重建
void SoundButtonWidget::ensureAssets() {
    const qreal dpr = devicePixelRatioF();
    if (!m_frames.isEmpty() && qFuzzyCompare(m_assetsDpr, dpr)) return;

    const QSize frameSize(qRound(buttonRect().width() * dpr),
                          qRound(buttonRect().height() * dpr));
    m_frames.clear();
    m_frames.reserve(kFrameCount);
    for (int i = 0; i < kFrameCount; ++i) {
        QPixmap pm(QStringLiteral(":/assets/button_%1.png").arg(i));
        pm = pm.scaled(frameSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pm.setDevicePixelRatio(dpr);
        m_frames.append(pm);
    }

    const int icon = qRound(kBadgeIconSize * dpr);
    // 未置顶：空心图钉 + 灰色；已置顶：实心图钉 + 主题蓝，一眼能分辨
    m_pinOff = tinted(QPixmap(QStringLiteral(":/assets/pin.png"))
                          .scaled(icon, icon, Qt::KeepAspectRatio, Qt::SmoothTransformation),
                      QColor(0x5f, 0x63, 0x68));
    m_pinOn = tinted(QPixmap(QStringLiteral(":/assets/pin_fill.png"))
                         .scaled(icon, icon, Qt::KeepAspectRatio, Qt::SmoothTransformation),
                     QColor(0x2b, 0x6b, 0xf2));
    m_pinOff.setDevicePixelRatio(dpr);
    m_pinOn.setDevicePixelRatio(dpr);
    m_assetsDpr = dpr;
}

void SoundButtonWidget::drawPin(QPainter &painter) {
    const QRect r = pinRect();
    const bool on = m_lib->alwaysOnTop();

    // 半透明白色圆底：不管桌面是什么颜色，图钉都看得清
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, on ? 240 : 170));
    painter.drawEllipse(r);
    if (m_pinPressed) {   // 按压反馈：按下时圆底加深一点
        painter.setBrush(QColor(0, 0, 0, on ? 30 : 20));
        painter.drawEllipse(r);
    }
    if (on) {   // 置顶时描一圈蓝边，状态更明确
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0x2b, 0x6b, 0xf2, 110), 1.6));
        painter.drawEllipse(r.adjusted(1, 1, -1, -1));
    }

    const int pad = (kBadgeSize - kBadgeIconSize) / 2;
    painter.drawPixmap(r.adjusted(pad, pad, -pad, -pad), on ? m_pinOn : m_pinOff);
}

void SoundButtonWidget::paintEvent(QPaintEvent *) {
    ensureAssets();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const SoundEntry *en = m_lib->entry(m_lib->currentIndex());
    const bool ready = en && en->ready;
    const bool failed = en && en->failed;

    // 状态只用"浓淡"表达：解码中半透明，解码失败更淡 + 右下角红点；窗口里不放文字
    p.setOpacity(failed ? 0.4 : (ready ? 1.0 : 0.55));
    const int frame = qBound(0, qRound(m_pressAnim->currentValue().toReal()), kFrameCount - 1);
    p.drawPixmap(buttonRect(), m_frames.at(frame));
    p.setOpacity(1.0);

    if (failed) {
        const QRect dot(buttonRect().right() - 20, buttonRect().bottom() - 20, 18, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xd9, 0x30, 0x25));
        p.drawEllipse(dot);
        p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(dot.center() - QPoint(0, 4), dot.center() + QPoint(0, 1));
        p.drawPoint(dot.center() + QPoint(0, 5));
    }

    drawPin(p);
}

void SoundButtonWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = e->globalPosition().toPoint();
    m_pressWindowTopLeft = pos();

    // 图钉：只切换置顶，不播音效也不触发按下动画
    if (pinRect().contains(e->pos())) {
        m_pinPressed = true;
        update();
        return;
    }

    m_pressUs = sbNowUs();   // 诊断用：记下按下时刻，推流后算端到端耗时
    animatePress();
    playCurrent();   // 按下即播，不等松开
    update();
}

void SoundButtonWidget::mouseMoveEvent(QMouseEvent *e) {
    if (!m_pressed) return;
    // 交互契约：位移超过 8px 才当拖动，此时停掉刚触发的误播并复位按下动画（不播回弹）；
    // 8px 以内仍算点击，声音继续播（手抖不会打断音效）
    if (!m_dragging &&
        (e->globalPosition().toPoint() - m_pressGlobal).manhattanLength() > kDragThreshold) {
        m_dragging = true;
        m_pendingPlay = false;
        m_pinPressed = false;
        resetPress();
        m_audio->stop();   // 确认是拖动而非点击，停掉误播（暂停，保住热流）
    }
    if (m_dragging)
        move(m_pressWindowTopLeft + e->globalPosition().toPoint() - m_pressGlobal);
}

void SoundButtonWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;

    if (m_pinPressed) {   // 松开时还在图钉上才算数（和普通按钮一样可以滑出去取消）
        m_pinPressed = false;
        if (pinRect().contains(e->pos())) toggleAlwaysOnTop();
    }
    // 松开不播动画：不管按住过多久、是不是拖动，都瞬时回到未按下姿态
    resetPress();

    if (m_dragging) {
        // 拖动结束才写配置，避免拖动过程中反复写文件
        m_lib->setWindowPos(pos());
        m_lib->saveConfig();
    }
    m_pressed = false;
    m_dragging = false;
}

void SoundButtonWidget::contextMenuEvent(QContextMenuEvent *e) {
    // 菜单结构：音效列表（单选）→ 添加/移除 → 音量 → 置顶 → 退出
    QMenu menu(this);

    const int n = m_lib->count();
    if (n == 0) {
        menu.addAction(tr("（列表为空，请添加音效）"))->setEnabled(false);
    } else {
        // 列表顶部固定一个「无」：选中后点击按钮不发声（按下动画照常），
        // 适合暂时想安静、又不想把音效从列表里删掉的情况
        QAction *noneAct = menu.addAction(tr("无（不播放音效）"));
        noneAct->setCheckable(true);
        noneAct->setChecked(m_lib->currentIndex() < 0);
        connect(noneAct, &QAction::triggered, this, [this] {
            m_lib->setCurrent(-1);
            m_audio->stop();   // 正在响的也立刻停掉：切到「无」就是马上安静
        });

        for (int i = 0; i < n; ++i) {
            const SoundEntry *en = m_lib->entry(i);
            QString label = en->name;
            if (en->failed) label += tr("（解码失败）");
            else if (!en->ready) label += tr("（加载中…）");
            QAction *a = menu.addAction(label);
            a->setCheckable(true);
            a->setChecked(i == m_lib->currentIndex());
            connect(a, &QAction::triggered, this, [this, i] {
                m_lib->setCurrent(i);
                playCurrent();
            });
        }
    }
    menu.addSeparator();

    menu.addAction(tr("添加音效…"), this, &SoundButtonWidget::addSounds);
    QAction *removeAct =
        menu.addAction(tr("移除当前音效"), this, &SoundButtonWidget::removeCurrent);
    removeAct->setEnabled(m_lib->currentIndex() >= 0);   // 选「无」时没有当前音效可移除
    menu.addSeparator();

    QMenu *volMenu = menu.addMenu(tr("音量"));
    auto *slider = new QSlider(Qt::Horizontal, &menu);
    slider->setRange(0, 100);
    slider->setValue(qRound(m_lib->volume() * 100));
    slider->setFixedWidth(170);
    connect(slider, &QSlider::valueChanged, this, [this](int v) {
        m_audio->setVolume(v / 100.0);
        m_lib->setVolume(v / 100.0);
    });
    connect(slider, &QSlider::sliderReleased, this,
            [this] { m_lib->saveConfig(); });
    auto *sliderAction = new QWidgetAction(volMenu);
    sliderAction->setDefaultWidget(slider);
    volMenu->addAction(sliderAction);

    // 和右上角图钉是同一个开关，两处都能改
    QAction *topAct = menu.addAction(tr("窗口置顶"));
    topAct->setCheckable(true);
    topAct->setChecked(m_lib->alwaysOnTop());
    connect(topAct, &QAction::triggered, this,
            &SoundButtonWidget::toggleAlwaysOnTop);
    menu.addSeparator();

    menu.addAction(tr("退出"), qApp, &QCoreApplication::quit);
    menu.exec(e->globalPos());
}

void SoundButtonWidget::playCurrent() {
    const qint64 pressUs = m_pressUs;   // 诊断：从按下到推流的耗时
    m_pressUs = 0;
    const SoundEntry *en = m_lib->entry(m_lib->currentIndex());
    if (!en) return;   // 列表为空或选的是「无」：按下不发声（按下动画照常播）
    if (en->failed) {
        // 解码失败的条目不发声，只在日志里留痕（按钮右下角有红点提示）
        qWarning() << "跳过解码失败的音效:" << en->path;
        return;
    }
    if (!en->ready) {   // 短音效通常几毫秒内就绪，就绪后自动补播
        m_pendingPlay = true;
        return;
    }
    // 到这里 PCM 已在内存：只剩下"换数据 + 起播"
    m_audio->play(en->pcm, en->format, pressUs);
}

// 把所有已解码格式的播放流预热好（首次调用会付设备枚举的固定成本）
void SoundButtonWidget::prepareFormats() {
    for (int i = 0; i < m_lib->count(); ++i)
        if (const SoundEntry *en = m_lib->entry(i); en && en->ready)
            m_audio->prepare(en->format);
}

// 动画唯一的播放入口：按下时从松开姿态正放到完全按下
void SoundButtonWidget::animatePress() {
    m_pressAnim->stop();
    m_pressAnim->setCurrentTime(0);   // stop() 会把值冻结在最后一帧，先 seek 回起点
    m_pressAnim->start();
}

// 松开 / 判定为拖动：瞬时回到未按下姿态。
// 不做倒放回弹、按住期间也不续播——动画只在按下那一刻被触发
void SoundButtonWidget::resetPress() {
    m_pressAnim->stop();
    m_pressAnim->setCurrentTime(0);
    update();
}

// 悬停提示：当前状态一句话 + 操作说明（切歌、解码完成、失败都会刷新）
void SoundButtonWidget::updateToolTip() {
    const SoundEntry *en = m_lib->entry(m_lib->currentIndex());
    QString tip;
    if (m_lib->isEmpty()) tip = tr("还没有音效，右键添加");
    else if (!en)         tip = tr("当前未选择音效（点击不播放）");
    else if (en->failed)  tip = tr("解码失败：%1").arg(en->name);
    else if (!en->ready)  tip = tr("加载中…");
    else                  tip = tr("当前音效：%1").arg(en->name);
    tip += tr("\n点击播放 · 拖动移动 · 图钉置顶 · 右键菜单");
    setToolTip(tip);
}

// 窗口里没有任何文字，切歌时用系统提示气泡报一下名字（1.2 秒后自动消失）
void SoundButtonWidget::flashName() {
    const SoundEntry *en = m_lib->entry(m_lib->currentIndex());
    QString text;
    if (en) {
        text = en->name;
        if (en->failed) text = tr("%1（解码失败）").arg(text);
        else if (!en->ready) text = tr("%1（加载中…）").arg(text);
    } else if (!m_lib->isEmpty()) {
        text = tr("无（不播放音效）");   // 切到「无」也要有反馈，不然像菜单没点上
    } else {
        return;
    }
    QToolTip::showText(mapToGlobal(QPoint(width() / 2, 0)), text, this, rect(), 1200);
}

void SoundButtonWidget::addSounds() {
    // 默认从 exe 旁的 sounds/ 目录开始找，方便用户把音效集中放一起
    const QString appDir = QCoreApplication::applicationDirPath();
    QString start = appDir + QStringLiteral("/sounds");
    if (!QDir(start).exists()) start = appDir;

    QStringList filter;
    for (const QString &ext : SoundLibrary::supportedExtensions())
        filter << QStringLiteral("*.") + ext;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("选择音效文件"), start,
        tr("音频文件 (%1);;所有文件 (*)").arg(filter.join(u' ')));
    if (files.isEmpty()) return;
    m_lib->addFiles(files);
    m_lib->saveConfig();
}

void SoundButtonWidget::removeCurrent() {
    m_lib->removeCurrent();
    m_lib->saveConfig();
}

void SoundButtonWidget::toggleAlwaysOnTop() {
    m_lib->setAlwaysOnTop(!m_lib->alwaysOnTop());
    setWindowFlag(Qt::WindowStaysOnTopHint, m_lib->alwaysOnTop());
    show();   // 改 flag 会隐藏窗口，需要重新显示
    m_lib->saveConfig();
    update();
}

void SoundButtonWidget::makeTrayIcon() {
    // 托盘图标直接用按钮素材，和窗口里保持一致
    QPixmap pm = QPixmap(QStringLiteral(":/assets/button_0.png"))
                     .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // 以 this 为 parent：不需要成员变量保管，窗口活着托盘就活着
    auto *tray = new QSystemTrayIcon(this);
    tray->setIcon(QIcon(pm));
    tray->setToolTip(tr("音效按钮"));
    auto *menu = new QMenu(this);
    menu->addAction(tr("显示/隐藏按钮"), this,
                    [this] { setVisible(!isVisible()); });
    menu->addAction(tr("退出"), qApp, &QCoreApplication::quit);
    tray->setContextMenu(menu);
    // 左键单击托盘 = 显示/隐藏按钮（和菜单里的那一项等价）
    connect(tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    setVisible(!isVisible());
            });
    tray->show();
}
