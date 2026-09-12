// 数据层实现：列表增删、QAudioDecoder 后台解码到内存 PCM、config.json 原子读写。
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

#include "LatencyLog.h"

const QStringList &SoundLibrary::supportedExtensions() {
    // 静态常量表：文件对话框的过滤器、sounds/ 目录自动导入、添加时的去重都用同一份
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
    const bool wasEmpty = m_entries.empty();
    bool added = false;
    for (const QString &p : paths) {
        // 统一成绝对路径再比较，避免同一个文件用不同相对路径写两遍
        const QString abs = QFileInfo(p).absoluteFilePath();
        const bool exists =
            std::any_of(m_entries.begin(), m_entries.end(),
                        [&abs](const auto &e) { return e->path == abs; });
        if (exists) continue;

        auto e = std::make_unique<SoundEntry>();
        e->path = abs;
        e->name = QFileInfo(abs).completeBaseName();
        // 先启动解码再放进列表：解码是异步的，entryReady 回来时才通知外界
        startDecode(e.get());
        m_entries.push_back(std::move(e));
        added = true;
    }
    if (!added) return;
    // 只有"从空的列表加进第一条"才自动选中；列表里本来就有音效时不抢当前项——
    // 用户可能显式选了「无」（m_current 同样是 -1），不该被一次添加顺手覆盖
    if (wasEmpty) {
        m_current = 0;
        emit currentChanged();
    }
    emit entriesChanged();
}

void SoundLibrary::removeCurrent() {
    if (m_current < 0 || m_current >= count()) return;
    m_entries.erase(m_entries.begin() + m_current);   // 正在解码的 entry 连同 decoder 一起销毁
    // 删最后一个时退到新的末尾；删空了就是 -1
    m_current = qBound(-1, m_current, count() - 1);
    emit entriesChanged();
    emit currentChanged();
}

void SoundLibrary::setCurrent(int index) {
    // 立即落盘：切换音效是用户显式意图，程序被强杀也不该丢
    // index = -1 合法，表示「无」（点击不发声）；其余越界值直接忽略
    if (index == m_current || index < -1 || index >= count()) return;
    m_current = index;
    saveConfig();
    emit currentChanged();
}

// 窗口层状态的读写：只改内存；落盘时机由 UI 决定（拖动结束 / 滑块松开 / 切歌时）
void SoundLibrary::setVolume(qreal v) { m_volume = qBound<qreal>(0.0, v, 1.0); }
void SoundLibrary::setWindowPos(const QPoint &p) { m_windowPos = p; }
void SoundLibrary::setAlwaysOnTop(bool on) { m_alwaysOnTop = on; }

void SoundLibrary::startDecode(SoundEntry *entry) {
    // 解码流程：QAudioDecoder 在内部线程边解边发 bufferReady，
    // 这里把数据攒进一个 shared_ptr<QByteArray>，解完再一次性交给播放端。
    // 用 shared_ptr 而不是 entry 自己的成员，是为了播放中删除条目时数据仍然活着。
    auto *dec = new QAudioDecoder(entry);
    entry->decoder = dec;
    auto pcm = std::make_shared<QByteArray>();   // 解码期间先攒在这里，完成后共享给播放端

    // 收尾（无论成功、失败还是"解到一半报错但有数据"都走这里）
    auto finish = [this, entry, pcm](bool ok) {
        // 早于 deleteLater 清掉指针：之后再来的信号一律直接 return
        if (entry->decoder) {
            entry->decoder->deleteLater();
            entry->decoder = nullptr;
        }
        // 音频数据原样保留：不做任何裁剪/改写，解码结果就是文件本身的内容
        entry->ready = ok && entry->format.isValid() && !pcm->isEmpty();
        if (!entry->ready) pcm->clear();
        entry->pcm = pcm;
        entry->failed = !entry->ready;
        if (entry->ready) {
            sbLatLog(QStringLiteral("解码完成 %1：%2Hz/%3ch 时长 %4ms")
                         .arg(entry->name)
                         .arg(entry->format.sampleRate())
                         .arg(entry->format.channelCount())
                         .arg(1000.0 * entry->pcm->size() /
                                  (entry->format.bytesPerFrame() * entry->format.sampleRate()),
                              0, 'f', 1));
        }
        int idx = -1;
        // 条目可能已被删除，这里重新找一次索引（找不到就是 -1，UI 会忽略）
        for (int i = 0; i < count(); ++i)
            if (m_entries[i].get() == entry) { idx = i; break; }
        emit entryReady(idx);
    };

    // bufferReady：解码器有数据可读，全部读空。格式以第一块为准，后续块应当是同一格式
    connect(dec, &QAudioDecoder::bufferReady, entry, [entry, dec, pcm] {
        if (!entry->decoder) return;
        while (dec->bufferAvailable()) {
            const QAudioBuffer buf = dec->read();
            if (!buf.isValid()) continue;
            if (!entry->format.isValid()) entry->format = buf.format();
            pcm->append(buf.constData<char>(), buf.byteCount());
        }
    });
    connect(dec, &QAudioDecoder::finished, entry, [finish] { finish(true); });
    // error 同时是信号名和取值函数名，需指明信号签名
    connect(dec,
            static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(
                &QAudioDecoder::error),
            entry, [entry, dec, pcm, finish](QAudioDecoder::Error) {
                if (!entry->decoder) return;
                if (!pcm->isEmpty()) {   // 已解出可用数据，末尾的小错误忽略
                    finish(true);
                    return;
                }
                // 一个字节都没解出来才算真失败
                finish(false);
                qWarning() << "解码失败:" << entry->path << dec->errorString();
            });

    // decoder 以 entry 为 parent：条目被移除时它一起析构，解码自然中止
    dec->setSource(QUrl::fromLocalFile(entry->path));
    // 异步解码，立刻返回——这就是"添加音效不卡界面"的原因
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
            // 文件被挪走/删掉的条目直接跳过，不让列表里出现无效项
            if (!path.isEmpty() && QFileInfo::exists(path)) paths << path;
        }
        addFiles(paths);

        // 恢复上次的选择：
        //  - lastPlayed 是路径：按路径找——列表增删、文件缺失都会让存下来的下标错位，路径不会
        //  - lastPlayed 是空串：上次选的是「无」，保持点击不发声
        //  - 没有 lastPlayed 键：老配置，退回按 current 下标
        int restore = -1;
        bool savedNone = false;
        if (o.contains(QStringLiteral("lastPlayed"))) {
            const QString lastPath = o.value(QStringLiteral("lastPlayed")).toString();
            if (lastPath.isEmpty()) {
                savedNone = true;
            } else {
                // 先归一成绝对路径再比：配置里可能是反斜杠等别的写法，条目路径是 absoluteFilePath 形式
                const QString want = QFileInfo(lastPath).absoluteFilePath();
                for (int i = 0; i < count(); ++i) {
                    if (m_entries[i]->path == want) {
                        restore = i;
                        break;
                    }
                }
            }
        }
        if (!savedNone && restore < 0) {
            const int saved = o.value(QStringLiteral("current")).toInt(-1);
            if (saved >= 0 && saved < count()) restore = saved;
        }
        if (savedNone) m_current = -1;
        else if (restore >= 0) m_current = restore;
        if (m_current >= count()) m_current = count() - 1;
        return;
    }

    // 首次运行（没有 config.json）：自动导入 exe 旁 sounds/ 目录里的音频，
    // 这样"解压到哪都能直接用"——把音效丢进 sounds/ 启动即可
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
    // 全部状态集中在一个文件里，便携：拷走整个目录即可带走列表、音量、窗口位置
    QJsonArray sounds;
    for (const auto &e : m_entries) sounds.append(e->path);

    QJsonObject o;
    o[QStringLiteral("sounds")] = sounds;
    o[QStringLiteral("current")] = m_current;
    // "上次播放"：存路径而不是下标（列表增删后下标会错位），启动时按它恢复当前音效；
    // 选的是「无」时写空串——这个键必须留着，启动时才能区分"上次选了无"和"老配置没这一项"
    if (const SoundEntry *cur = entry(m_current))
        o[QStringLiteral("lastPlayed")] = cur->path;
    else
        o[QStringLiteral("lastPlayed")] = QString();
    o[QStringLiteral("volume")] = m_volume;
    o[QStringLiteral("pos")] = QJsonArray{m_windowPos.x(), m_windowPos.y()};
    o[QStringLiteral("alwaysOnTop")] = m_alwaysOnTop;

    QSaveFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}
