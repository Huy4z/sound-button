#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QPoint>
#include <QStringList>
#include <memory>
#include <vector>

class QAudioDecoder;

// 单个音效：文件路径 + 解码后的内存 PCM。
// 启动/添加时即后台解码（并裁掉首尾静音），点击时不再经历解码和媒体管线，保证低延迟。
class SoundEntry : public QObject
{
public:
    using QObject::QObject;

    QString path;                     // 绝对路径：去重、写进 config.json 都用它
    QString name;                     // 显示名，取文件名（去掉扩展名）
    std::shared_ptr<QByteArray> pcm;  // 与 AudioEngine 共享持有：播放中删条目也安全
    QAudioFormat format;              // 解码后的采样格式，AudioEngine 按它挑热流
    double trimmedLeadMs = 0;         // 解码时裁掉的开头静音长度
    bool ready = false;               // 解码完成且数据可用，可以播
    bool failed = false;              // 解码失败（按钮显示红色，点击时跳过）
    QAudioDecoder *decoder = nullptr; // 以 entry 为 parent，随 entry 销毁
};

// 音效列表模型：增删切换、预解码调度、config.json 持久化。
class SoundLibrary : public QObject
{
    Q_OBJECT
public:
    explicit SoundLibrary(QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_entries.size()); }
    bool isEmpty() const { return m_entries.empty(); }
    // 越界返回 nullptr；返回的指针归库所有，条目被删后失效
    const SoundEntry *entry(int index) const;
    int currentIndex() const { return m_current; }

    // 批量添加（按绝对路径去重，逐个启动后台解码）；首个音效自动成为当前项
    void addFiles(const QStringList &paths);
    // 删除当前项，并把光标落到相邻音效上
    void removeCurrent();
    // 切换当前音效（会立即写 config.json，重启后仍停在这个音效上）
    void setCurrent(int index);

    // 窗口层状态也统一存进 config.json
    qreal volume() const { return m_volume; }
    void setVolume(qreal v);
    QPoint windowPos() const { return m_windowPos; }
    void setWindowPos(const QPoint &p);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool on);

    void loadConfig(); // 没有 config.json 时自动导入 exe 旁 sounds/ 目录；有则恢复上次播放的音效
    void saveConfig(); // 用 QSaveFile 原子写入，避免断电写坏配置

    // 能解码的扩展名（实际支持范围由 FFmpeg 后端决定），文件对话框和自动导入共用
    static const QStringList &supportedExtensions();

signals:
    void entriesChanged();      // 列表增删：UI 重绘
    void currentChanged();      // 当前项变化：UI 重绘
    void entryReady(int index); // 某条解码完成：UI 顺手预热它的格式，必要时补播

private:
    void startDecode(SoundEntry *entry);
    QString configPath() const;

    std::vector<std::unique_ptr<SoundEntry>> m_entries;
    int m_current = -1;
    qreal m_volume = 0.9;
    QPoint m_windowPos{120, 120};
    bool m_alwaysOnTop = true;
};
