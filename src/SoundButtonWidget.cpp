#include "SoundButtonWidget.h"

#include "AudioEngine.h"
#include "SoundLibrary.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSlider>
#include <QSystemTrayIcon>
#include <QWheelEvent>
#include <QWidgetAction>

namespace {
constexpr int kDragThreshold = 8;

// 喇叭主体（小方块 + 外张喇叭口），在给定矩形内按比例绘制
QPolygonF speakerBody(const QRectF &r) {
    return {
        {r.left(),                       r.top() + r.height() * 0.38},
        {r.left() + r.width() * 0.22,    r.top() + r.height() * 0.38},
        {r.left() + r.width() * 0.45,    r.top() + r.height() * 0.16},
        {r.left() + r.width() * 0.45,    r.top() + r.height() * 0.84},
        {r.left() + r.width() * 0.22,    r.top() + r.height() * 0.62},
        {r.left(),                       r.top() + r.height() * 0.62}};
}
}  // namespace

SoundButtonWidget::SoundButtonWidget(QWidget *parent) : QWidget(parent) {
    m_lib = new SoundLibrary(this);
    m_audio = new AudioEngine(this);
    m_lib->loadConfig();

    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (m_lib->alwaysOnTop()) flags |= Qt::WindowStaysOnTopHint;
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(190, 54);
    setWindowTitle(tr("音效按钮"));

    move(m_lib->windowPos());
    m_audio->setVolume(m_lib->volume());

    connect(m_lib, &SoundLibrary::entriesChanged, this,
            qOverload<>(&QWidget::update));
    connect(m_lib, &SoundLibrary::currentChanged, this, [this] {
        m_pendingPlay = false;
        update();
    });
    connect(m_lib, &SoundLibrary::entryReady, this, [this](int index) {
        if (index == m_lib->currentIndex() && m_pendingPlay) {
            m_pendingPlay = false;
            playCurrent();
        }
        update();
    });

    makeTrayIcon();
}

void SoundButtonWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const SoundEntry *en = m_lib->entry(m_lib->currentIndex());
    QColor base;
    if (!en)             base = QColor(64, 68, 76, 235);
    else if (en->failed) base = QColor(0xb0, 0x36, 0x40, 240);
    else if (!en->ready) base = QColor(96, 102, 112, 240);
    else                 base = QColor(0x2b, 0x6b, 0xf2, 240);
    if (m_pressed && !m_dragging) base = base.darker(112);

    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(Qt::NoPen);
    p.setBrush(base);
    p.drawRoundedRect(r, 20, 20);

    // 自绘喇叭图标，不依赖任何图标资源
    const QRectF iconRect(r.left() + 13, r.center().y() - 11, 22, 22);
    p.setBrush(Qt::white);
    p.drawPolygon(speakerBody(iconRect));
    if (en && en->ready) {
        QPen wave(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap);
        p.setPen(wave);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(iconRect.left() + 11, iconRect.center().y() - 4, 8, 8),
                  -50 * 16, 100 * 16);
        p.drawArc(QRectF(iconRect.left() + 11, iconRect.center().y() - 7.5, 15, 15),
                  -50 * 16, 100 * 16);
    }

    QFont f = p.font();
    f.setPointSizeF(10.5);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    QString text;
    if (!en)             text = tr("右键添加音效");
    else if (en->failed) text = tr("解码失败：%1").arg(en->name);
    else if (!en->ready) text = tr("加载中…");
    else                 text = en->name;
    const qreal textLeft = iconRect.right() + 9;
    const qreal textWidth = r.right() - 14 - textLeft;
    const QString elided =
        QFontMetrics(f).elidedText(text, Qt::ElideRight, int(textWidth));
    p.drawText(QRectF(textLeft, r.top(), textWidth, r.height()),
               Qt::AlignVCenter, elided);
}

void SoundButtonWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_pressed = true;
    m_dragging = false;
    m_pressGlobal = e->globalPosition().toPoint();
    m_pressWindowTopLeft = pos();
    playCurrent();   // 按下即播，不等松开
    update();
}

void SoundButtonWidget::mouseMoveEvent(QMouseEvent *e) {
    if (!m_pressed) return;
    if (!m_dragging &&
        (e->globalPosition().toPoint() - m_pressGlobal).manhattanLength() >
            kDragThreshold) {
        m_dragging = true;
        m_pendingPlay = false;
        m_audio->stop();   // 确认是拖动而非点击，停掉误播
    }
    if (m_dragging)
        move(m_pressWindowTopLeft + e->globalPosition().toPoint() - m_pressGlobal);
}

void SoundButtonWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    if (m_dragging) {
        m_lib->setWindowPos(pos());
        m_lib->saveConfig();
    }
    m_pressed = false;
    m_dragging = false;
    update();
}

void SoundButtonWidget::wheelEvent(QWheelEvent *e) {
    const int n = m_lib->count();
    if (n == 0) return;
    const int step = e->angleDelta().y() > 0 ? -1 : 1;
    m_lib->setCurrent((m_lib->currentIndex() + step + n) % n);
}

void SoundButtonWidget::contextMenuEvent(QContextMenuEvent *e) {
    QMenu menu(this);

    const int n = m_lib->count();
    if (n == 0) {
        menu.addAction(tr("（列表为空，请添加音效）"))->setEnabled(false);
    } else {
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
    removeAct->setEnabled(n > 0);
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
    const SoundEntry *en = m_lib->entry(m_lib->currentIndex());
    if (!en) return;
    if (en->failed) {
        qWarning() << "跳过解码失败的音效:" << en->path;
        return;
    }
    if (!en->ready) {   // 短音效通常几毫秒内就绪，就绪后自动补播
        m_pendingPlay = true;
        return;
    }
    m_audio->play(en->pcm, en->format);
}

void SoundButtonWidget::addSounds() {
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
}

void SoundButtonWidget::makeTrayIcon() {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x2b, 0x6b, 0xf2));
        p.drawRoundedRect(0, 0, 32, 32, 8, 8);
        p.setBrush(Qt::white);
        p.drawPolygon(speakerBody(QRectF(6, 9, 14, 14)));
        p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(20, 12, 6, 8), -50 * 16, 100 * 16);
        p.drawArc(QRectF(20, 9, 11, 14), -50 * 16, 100 * 16);
    }

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon(pm));
    m_tray->setToolTip(tr("音效按钮"));
    auto *menu = new QMenu(this);
    menu->addAction(tr("显示/隐藏按钮"), this,
                    [this] { setVisible(!isVisible()); });
    menu->addAction(tr("退出"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    setVisible(!isVisible());
            });
    m_tray->show();
}
