#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QPoint>
#include <QStringList>
#include <memory>
#include <vector>

class QAudioDecoder;

/// 单个音效：文件路径 + 解码后的内存 PCM。
/// 启动/添加时即后台解码（音频数据原样保留，不做裁剪/改写），点击时不再经历解码和媒体管线，保证低延迟。
class SoundEntry : public QObject
{
public:
    using QObject::QObject;

    QString path;                     ///< 绝对路径：去重、写进 config.json 都用它
    QString name;                     ///< 显示名，取文件名（去掉扩展名）
    std::shared_ptr<QByteArray> pcm;  ///< 与 AudioEngine 共享持有：播放中删条目也安全
    QAudioFormat format;              ///< 解码后的采样格式，AudioEngine 按它挑热流
    bool ready = false;               ///< 解码完成且数据可用，可以播
    bool failed = false;              ///< 解码失败（按钮右下角红点，点击时跳过）
    QAudioDecoder *decoder = nullptr; ///< 解码器句柄（parent 是 entry）；结束后置空，之后的信号一律忽略
};

/// 音效列表模型：增删切换、后台预解码调度、config.json 持久化。
/// 不认识播放层：只把解码好的 PCM（shared_ptr）交给 UI 层转手，保持数据层与播放层互不相识。
class SoundLibrary : public QObject
{
    Q_OBJECT
public:
    explicit SoundLibrary(QObject *parent = nullptr);

    /// 列表里的音效条数；列表当前是否为空
    int count() const { return static_cast<int>(m_entries.size()); }
    bool isEmpty() const { return m_entries.empty(); }
    /// 按下标取条目；越界返回 nullptr。指针归库所有，条目被删后立即失效
    const SoundEntry *entry(int index) const;
    /// 当前选中项下标；-1 = 菜单顶部的「无」（点击不发声）
    int currentIndex() const { return m_current; }

    /// 批量添加（按绝对路径去重，逐个启动后台解码）。
    /// 只有"从空列表加进第一条"才自动选中它；已选「无」时添加不会抢走当前选择
    void addFiles(const QStringList &paths);
    /// 删除当前项（正在解码的连同解码器一起销毁），光标落到相邻音效上
    void removeCurrent();
    /// 切换当前音效（-1 = 「无」）。会立即写 config.json——切歌是用户显式意图，强杀也不该丢
    void setCurrent(int index);

    /// 窗口层状态（音量 0~1 / 位置为逻辑坐标 / 置顶），统一存进 config.json
    qreal volume() const { return m_volume; }
    void setVolume(qreal v);
    QPoint windowPos() const { return m_windowPos; }
    void setWindowPos(const QPoint &p);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool on);

    /// 读 config.json：没有文件时自动导入 exe 旁 sounds/ 目录；
    /// 有则恢复上次选择（lastPlayed 存路径而不是下标，空串 = 上次选的是「无」）
    void loadConfig();
    /// 用 QSaveFile 原子写入，避免断电写坏配置
    void saveConfig();

    /// 能解码的扩展名（实际支持范围由 FFmpeg 后端决定），文件对话框和自动导入共用
    static const QStringList &supportedExtensions();

signals:
    void entriesChanged();      ///< 列表增删：UI 重绘
    void currentChanged();      ///< 当前项变化：UI 取消挂起补播、刷新 tooltip 并重绘
    void entryReady(int index); ///< 某条解码完成（index 无效 = 条目已被删）：UI 顺手预热格式，必要时补播

private:
    /// 启动某条的后台解码；解码器以 entry 为 parent，条目删除即中止
    void startDecode(SoundEntry *entry);
    /// config.json 的完整路径（exe 同目录，便携）
    QString configPath() const;

    std::vector<std::unique_ptr<SoundEntry>> m_entries; ///< 列表本体，顺序即菜单顺序
    int m_current = -1;                                 ///< 当前项下标；-1 = 「无」
    qreal m_volume = 0.9;                               ///< 音量 0~1
    QPoint m_windowPos{120, 120};                       ///< 窗口位置（逻辑坐标）
    bool m_alwaysOnTop = true;                          ///< 置顶状态（图钉与菜单共用）
};
