#include "SoundLibrary.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

const QStringList &SoundLibrary::supportedExtensions() {
    static const QStringList kExts{
        QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"),
        QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("opus"), QStringLiteral("wma"), QStringLiteral("mp2"),
        QStringLiteral("aif"), QStringLiteral("aiff")};
    return kExts;
}

SoundLibrary::SoundLibrary(QObject *parent) : QObject(parent) {}

const SoundEntry *SoundLibrary::entry(int index) const {
    return index >= 0 && index < count() ? m_entries[index].get() : nullptr;
}

void SoundLibrary::addFiles(const QStringList &paths) {
    bool added = false;
    for (const QString &p : paths) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        const bool exists =
            std::any_of(m_entries.begin(), m_entries.end(),
                        [&abs](const auto &e) { return e->path == abs; });
        if (exists) continue;

        auto e = std::make_unique<SoundEntry>();
        e->path = abs;
        e->name = QFileInfo(abs).completeBaseName();
        startDecode(e.get());
        m_entries.push_back(std::move(e));
        added = true;
    }
    if (!added) return;
    if (m_current < 0 && !m_entries.empty()) {
        m_current = 0;
        emit currentChanged();
    }
    emit entriesChanged();
}

void SoundLibrary::removeCurrent() {
    if (m_current < 0 || m_current >= count()) return;
    m_entries.erase(m_entries.begin() + m_current);   // 正在解码的 entry 连同 decoder 一起销毁
    m_current = qBound(-1, m_current, count() - 1);
    emit entriesChanged();
    emit currentChanged();
}

void SoundLibrary::setCurrent(int index) {
    if (index == m_current || index < 0 || index >= count()) return;
    m_current = index;
    saveConfig();
    emit currentChanged();
}

void SoundLibrary::setVolume(qreal v) { m_volume = qBound<qreal>(0.0, v, 1.0); }
void SoundLibrary::setWindowPos(const QPoint &p) { m_windowPos = p; }
void SoundLibrary::setAlwaysOnTop(bool on) { m_alwaysOnTop = on; }

void SoundLibrary::startDecode(SoundEntry *entry) {
    auto *dec = new QAudioDecoder(entry);
    entry->decoder = dec;

    auto finish = [this, entry](bool ok) {
        entry->ready = ok && entry->format.isValid() && !entry->pcm.isEmpty();
        entry->failed = !entry->ready;
        if (entry->decoder) {
            entry->decoder->deleteLater();
            entry->decoder = nullptr;
        }
        int idx = -1;
        for (int i = 0; i < count(); ++i)
            if (m_entries[i].get() == entry) { idx = i; break; }
        emit entryReady(idx);
    };

    connect(dec, &QAudioDecoder::bufferReady, entry, [this, entry, dec] {
        if (!entry->decoder) return;
        while (dec->bufferAvailable()) {
            const QAudioBuffer buf = dec->read();
            if (!buf.isValid()) continue;
            if (!entry->format.isValid()) entry->format = buf.format();
            entry->pcm.append(buf.constData<char>(), buf.byteCount());
        }
    });
    connect(dec, &QAudioDecoder::finished, entry, [finish] { finish(true); });
    // error 同时是信号名和取值函数名，需指明信号签名
    connect(dec,
            static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(
                &QAudioDecoder::error),
            entry, [this, entry, dec, finish](QAudioDecoder::Error) {
                if (!entry->decoder) return;
                if (!entry->pcm.isEmpty()) {   // 已解出可用数据，末尾的小错误忽略
                    finish(true);
                    return;
                }
                finish(false);
                qWarning() << "解码失败:" << entry->path << dec->errorString();
            });

    dec->setSource(QUrl::fromLocalFile(entry->path));
    dec->start();
}

QString SoundLibrary::configPath() const {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/config.json");
}

void SoundLibrary::loadConfig() {
    QFile f(configPath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_volume = qBound<qreal>(0.0, o.value(QStringLiteral("volume")).toDouble(0.9), 1.0);
        const QJsonArray pos = o.value(QStringLiteral("pos")).toArray();
        if (pos.size() == 2)
            m_windowPos = QPoint(pos.at(0).toInt(), pos.at(1).toInt());
        m_alwaysOnTop = o.value(QStringLiteral("alwaysOnTop")).toBool(true);

        QStringList paths;
        for (const auto &v : o.value(QStringLiteral("sounds")).toArray()) {
            const QString path = v.toString();
            if (!path.isEmpty() && QFileInfo::exists(path)) paths << path;
        }
        addFiles(paths);
        if (m_current >= count()) m_current = count() - 1;
        return;
    }

    // 首次运行：自动导入 exe 旁 sounds/ 目录里的音频
    QDir soundsDir(QCoreApplication::applicationDirPath() + QStringLiteral("/sounds"));
    if (!soundsDir.exists()) return;
    QStringList nameFilters;
    for (const QString &ext : supportedExtensions())
        nameFilters << QStringLiteral("*.") + ext;
    QStringList files;
    for (const QString &name :
         soundsDir.entryList(nameFilters, QDir::Files, QDir::Name))
        files << soundsDir.absoluteFilePath(name);
    addFiles(files);
    if (!isEmpty()) saveConfig();
}

void SoundLibrary::saveConfig() {
    QJsonArray sounds;
    for (const auto &e : m_entries) sounds.append(e->path);

    QJsonObject o;
    o[QStringLiteral("sounds")] = sounds;
    o[QStringLiteral("current")] = m_current;
    o[QStringLiteral("volume")] = m_volume;
    o[QStringLiteral("pos")] = QJsonArray{m_windowPos.x(), m_windowPos.y()};
    o[QStringLiteral("alwaysOnTop")] = m_alwaysOnTop;

    QSaveFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}
