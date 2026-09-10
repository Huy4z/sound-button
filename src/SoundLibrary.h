#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QPoint>
#include <QStringList>
#include <memory>
#include <vector>

class QAudioDecoder;

// 单个音效：文件路径 + 解码后的内存 PCM。
// 启动/添加时即后台解码，点击时不再经历解码和媒体管线，保证低延迟。
class SoundEntry : public QObject {
public:
    using QObject::QObject;

    QString path;
    QString name;
    QByteArray pcm;
    QAudioFormat format;
    bool ready = false;
    bool failed = false;
    QAudioDecoder *decoder = nullptr;   // 以 entry 为 parent，随 entry 销毁
};

// 音效列表模型：增删切换、预解码调度、config.json 持久化。
class SoundLibrary : public QObject {
    Q_OBJECT
public:
    explicit SoundLibrary(QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_entries.size()); }
    bool isEmpty() const { return m_entries.empty(); }
    const SoundEntry *entry(int index) const;
    int currentIndex() const { return m_current; }

    void addFiles(const QStringList &paths);
    void removeCurrent();
    void setCurrent(int index);

    // 窗口层状态也统一存进 config.json
    qreal volume() const { return m_volume; }
    void setVolume(qreal v);
    QPoint windowPos() const { return m_windowPos; }
    void setWindowPos(const QPoint &p);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool on);

    void loadConfig();
    void saveConfig();

    static const QStringList &supportedExtensions();

signals:
    void entriesChanged();
    void currentChanged();
    void entryReady(int index);

private:
    void startDecode(SoundEntry *entry);
    QString configPath() const;

    std::vector<std::unique_ptr<SoundEntry>> m_entries;
    int m_current = -1;
    qreal m_volume = 0.9;
    QPoint m_windowPos{120, 120};
    bool m_alwaysOnTop = true;
};
